#include "datadog_agent.h"

#include <cassert>
#include <chrono>
#include <string>
#include <typeinfo>
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

const StringView traces_api_path = "/v0.4/traces";

HTTPClient::URL traces_endpoint(const HTTPClient::URL& agent_url) {
  auto traces_url = agent_url;
  append(traces_url.path, traces_api_path);
  return traces_url;
}

Expected<void> msgpack_encode(
    std::string& destination,
    const std::vector<std::unique_ptr<SpanData>>& spans) {
  return msgpack::pack_array(destination, spans,
                             [](auto& destination, const auto& span_ptr) {
                               assert(span_ptr);
                               return msgpack_encode(destination, *span_ptr);
                             });
}

Expected<void> msgpack_encode(
    std::string& destination,
    const std::vector<DatadogAgent::TraceChunk>& trace_chunks) {
  return msgpack::pack_array(destination, trace_chunks,
                             [](auto& destination, const auto& chunk) {
                               return msgpack_encode(destination, chunk.spans);
                             });
}

std::variant<CollectorResponse, std::string> parse_agent_traces_response(
    StringView body) try {
  nlohmann::json response = nlohmann::json::parse(body);

  StringView type = response.type_name();
  if (type != "object") {
    std::string message;
    message +=
        "Parsing the Datadog Agent's response to traces we sent it failed.  "
        "The response is expected to be a JSON object, but instead it's a JSON "
        "value with type \"";
    append(message, type);
    message += '\"';
    message += "\nError occurred for response body (begins on next line):\n";
    append(message, body);
    return message;
  }

  const StringView sample_rates_property = "rate_by_service";
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
    append(message, sample_rates_property);
    message +=
        "\" property of the response is expected to be a JSON object, but "
        "instead it's a JSON value with type \"";
    append(message, type);
    message += '\"';
    message += "\nError occurred for response body (begins on next line):\n";
    append(message, body);
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
      append(message, type);
      message += "\" instead.";
      message += "\nError occurred for response body (begins on next line):\n";
      append(message, body);
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
      append(message, body);
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
  append(message, body);
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
          config.flush_interval, [this]() { flush(); })),
      flush_interval_(config.flush_interval) {
  assert(logger_);
}

DatadogAgent::~DatadogAgent() {
  const auto deadline = clock_().tick + std::chrono::seconds(2);
  cancel_scheduled_flush_();
  flush();
  http_client_->drain(deadline);
}

Expected<void> DatadogAgent::send(
    std::vector<std::unique_ptr<SpanData>>&& spans,
    const std::shared_ptr<TraceSampler>& response_handler) {
  std::lock_guard<std::mutex> lock(mutex_);
  trace_chunks_.push_back(TraceChunk{std::move(spans), response_handler});
  return nullopt;
}

nlohmann::json DatadogAgent::config_json() const {
  const auto& url = traces_endpoint_;  // brevity
  const auto flush_interval_milliseconds =
      std::chrono::duration_cast<std::chrono::milliseconds>(flush_interval_)
          .count();

  // clang-format off
  return nlohmann::json::object({
    {"type", "datadog::tracing::DatadogAgent"},
    {"config", nlohmann::json::object({
      {"url", (url.scheme + "://" + url.authority + url.path)},
      {"flush_interval_milliseconds", flush_interval_milliseconds},
      {"http_client", http_client_->config_json()},
      {"event_scheduler", event_scheduler_->config_json()},
    })},
  });
  // clang-format on
}

void DatadogAgent::flush() {
  std::vector<TraceChunk> trace_chunks;
  {
    std::lock_guard<std::mutex> lock(mutex_);
    using std::swap;
    swap(trace_chunks, trace_chunks_);
  }

  if (trace_chunks.empty()) {
    return;
  }

  std::string body;
  auto encode_result = msgpack_encode(body, trace_chunks);
  if (auto* error = encode_result.if_error()) {
    logger_->log_error(*error);
    return;
  }

  // One HTTP request to the Agent could possibly involve trace chunks from
  // multiple tracers, and thus multiple trace samplers might need to have
  // their rates updated. Unlikely, but possible.
  std::unordered_set<std::shared_ptr<TraceSampler>> response_handlers;
  for (auto& chunk : trace_chunks) {
    response_handlers.insert(std::move(chunk.response_handler));
  }

  // This is the callback for setting request headers.
  // It's invoked synchronously (before `post` returns).
  auto set_request_headers = [&](DictWriter& headers) {
    headers.set("Content-Type", "application/msgpack");
    headers.set("Datadog-Meta-Lang", "cpp");
    headers.set("Datadog-Meta-Lang-Version", std::to_string(__cplusplus));
    headers.set("Datadog-Meta-Tracer-Version", tracer_version);
    headers.set("X-Datadog-Trace-Count", std::to_string(trace_chunks.size()));
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
    logger->log_error(
        error.with_prefix("Error occurred during HTTP request: "));
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
