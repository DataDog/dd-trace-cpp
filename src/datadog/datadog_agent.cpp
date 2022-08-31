#include "datadog_agent.h"

#include <cassert>
#include <chrono>
#include <exception>
#include <string>
#include <unordered_map>
#include <unordered_set>

#include "collector_response.h"
#include "datadog_agent_config.h"
#include "dict_writer.h"
#include "json.hpp"
#include "logger.h"
#include "msgpack.h"
#include "span_data.h"
#include "trace_sampler.h"
#include "version.h"

namespace datadog {
namespace tracing {
namespace {

const std::string_view traces_api_path = "/v0.4/traces";

HTTPClient::URL traces_endpoint(const HTTPClient::URL& agent_url) {
  auto traces_url = agent_url;
  traces_url.path += traces_api_path;
  return traces_url;
}

Expected<void> msgpack_encode(
    std::string& destination,
    const std::vector<std::unique_ptr<SpanData>>& spans) try {
  msgpack::pack_array(destination, spans.size());

  for (const auto& span_ptr : spans) {
    assert(span_ptr);
    auto result = msgpack_encode(destination, *span_ptr);
    if (auto* error = result.if_error()) {
      return std::move(*error);
    }
  }

  return std::nullopt;
} catch (const std::exception& error) {
  return Error{Error::MESSAGEPACK_ENCODE_FAILURE, error.what()};
}

Expected<void> msgpack_encode(
    std::string& destination,
    const std::vector<DatadogAgent::TraceChunk>& trace_chunks) try {
  msgpack::pack_array(destination, trace_chunks.size());

  for (const auto& chunk : trace_chunks) {
    auto result = msgpack_encode(destination, chunk.spans);
    if (auto* error = result.if_error()) {
      return std::move(*error);
    }
  }

  return std::nullopt;
} catch (const std::exception& error) {
  return Error{Error::MESSAGEPACK_ENCODE_FAILURE, error.what()};
}

std::variant<CollectorResponse, std::string> parse_agent_traces_response(
    std::string_view body) try {
  nlohmann::json response = nlohmann::json::parse(body);

  std::string_view type = response.type_name();
  if (type != "object") {
    std::string message;
    message +=
        "Parsing the Datadog Agent's response to traces we sent it failed.  "
        "The response is expected to be a JSON object, but instead it's a JSON "
        "value with type \"";
    message += type;
    message += '\"';
    message += "\nError occurred for response body (begins on next line):\n";
    message += body;
    return message;
  }

  const std::string_view sample_rates_property = "rate_by_service";
  const auto found = response.find(sample_rates_property);
  if (found == response.end()) {
    return CollectorResponse{};
  }
  const auto& rates_json = found.value();
  type = rates_json.type_name();
  if (type != "object") {
    std::string message;
    message +=
        "Parsing the Datadog Agent's response to traces we sent it failed.  "
        "The \"";
    message += sample_rates_property;
    message +=
        "\" property of the response is expected to be a JSON object, but "
        "instead it's a JSON value with type \"";
    message += type;
    message += '\"';
    message += "\nError occurred for response body (begins on next line):\n";
    message += body;
    return message;
  }

  std::unordered_map<std::string, Rate> sample_rates;
  for (const auto& [key, value] : rates_json.items()) {
    type = value.type_name();
    if (type != "number") {
      std::string message;
      message +=
          "Datadog Agent response to traces included an invalid sample rate "
          "for the key \"";
      message += key;
      message += "\". Rate should be a number, but it's a \"";
      message += type;
      message += "\" instead.";
      message += "\nError occurred for response body (begins on next line):\n";
      message += body;
      return message;
    }
    auto maybe_rate = Rate::from(value);
    if (auto* error = maybe_rate.if_error()) {
      std::string message;
      message +=
          "Datadog Agent response trace traces included an invalid sample rate "
          "for the key \"";
      message += key;
      message += "\": ";
      message += error->message;
      message += "\nError occurred for response body (begins on next line):\n";
      message += body;
      return message;
    }
    sample_rates.emplace(key, *maybe_rate);
  }
  return CollectorResponse{std::move(sample_rates)};
} catch (const nlohmann::json::exception& error) {
  std::string message;
  message +=
      "Parsing the Datadog Agent's response to traces we sent it failed with a "
      "JSON error: ";
  message += error.what();
  message += "\nError occurred for response body (begins on next line):\n";
  message += body;
  return message;
}

}  // namespace

DatadogAgent::DatadogAgent(const FinalizedDatadogAgentConfig& config,
                           const Clock& clock,
                           const std::shared_ptr<Logger>& logger)
    : clock_(clock),
      logger_(logger),
      traces_endpoint_(traces_endpoint(config.url)),
      http_client_(config.http_client),
      event_scheduler_(config.event_scheduler),
      cancel_scheduled_flush_(event_scheduler_->schedule_recurring_event(
          config.flush_interval, [this]() { flush(); })) {
  assert(logger_);
}

DatadogAgent::~DatadogAgent() {
  cancel_scheduled_flush_();
  http_client_->drain(clock_().tick + std::chrono::seconds(2));
}

Expected<void> DatadogAgent::send(
    std::vector<std::unique_ptr<SpanData>>&& spans,
    const std::shared_ptr<TraceSampler>& response_handler) {
  std::lock_guard<std::mutex> lock(mutex_);
  incoming_trace_chunks_.push_back(
      TraceChunk{std::move(spans), response_handler});
  return std::nullopt;
}

void DatadogAgent::flush() {
  outgoing_trace_chunks_.clear();
  {
    std::lock_guard<std::mutex> lock(mutex_);
    using std::swap;
    swap(incoming_trace_chunks_, outgoing_trace_chunks_);
  }

  if (outgoing_trace_chunks_.empty()) {
    return;
  }

  std::string body;
  auto encode_result = msgpack_encode(body, outgoing_trace_chunks_);
  if (auto* error = encode_result.if_error()) {
    logger_->log_error(*error);
    return;
  }

  // One HTTP request to the Agent could possibly involve trace chunks from
  // multiple tracers, and thus multiple trace samplers might need to have
  // their rates updated. Unlikely, but possible.
  std::unordered_set<std::shared_ptr<TraceSampler>> response_handlers;
  for (auto& chunk : outgoing_trace_chunks_) {
    response_handlers.insert(std::move(chunk.response_handler));
  }

  // This is the callback for setting request headers.
  // It's invoked synchronously (before `post` returns).
  auto set_request_headers = [this](DictWriter& headers) {
    headers.set("Content-Type", "application/msgpack");
    headers.set("Datadog-Meta-Lang", "cpp");
    headers.set("Datadog-Meta-Lang-Version", std::to_string(__cplusplus));
    headers.set("Datadog-Meta-Tracer-Version", tracer_version);
    headers.set("X-Datadog-Trace-Count",
                std::to_string(outgoing_trace_chunks_.size()));
  };

  // This is the callback for the HTTP response.  It's invoked
  // asynchronously.
  auto on_response = [samplers = std::move(response_handlers),
                      logger = logger_](int response_status,
                                        const DictReader& /*response_headers*/,
                                        std::string response_body) {
    if (response_status < 200 || response_status >= 300) {
      logger->log_error([&](auto& stream) {
        stream << "Unexpected response status " << response_status
               << " with body (starts on next line):\n"
               << response_body;
      });
      return;
    }

    auto result = parse_agent_traces_response(response_body);
    if (const auto* error_message = std::get_if<std::string>(&result)) {
      logger->log_error(*error_message);
      return;
    }
    const auto& response = std::get<CollectorResponse>(result);
    for (const auto& sampler : samplers) {
      if (sampler) {
        sampler->handle_collector_response(response);
      }
    }
  };

  // This is the callback for if something goes wrong sending the
  // request or retrieving the response.  It's invoked
  // asynchronously.
  auto on_error = [logger = logger_](Error error) {
    logger->log_error([&](auto& stream) {
      stream << "Error occurred during HTTP request: " << error;
    });
  };

  auto post_result = http_client_->post(
      traces_endpoint_, std::move(set_request_headers), std::move(body),
      std::move(on_response), std::move(on_error));
  if (auto* error = post_result.if_error()) {
    logger_->log_error(*error);
  }
}

}  // namespace tracing
}  // namespace datadog
