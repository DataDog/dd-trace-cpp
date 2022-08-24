#include "datadog_agent.h"

#include <cassert>
#include <exception>
#include <iostream>  // TODO: no
#include <string>
#include <unordered_map>
#include <unordered_set>

#include "collector_response.h"
#include "datadog_agent_config.h"
#include "dict_writer.h"
#include "json.hpp"
#include "msgpack.h"
#include "span_data.h"
#include "trace_sampler.h"

namespace datadog {
namespace tracing {
namespace {

const std::string_view traces_api_path = "/v0.4/traces";

HTTPClient::URL traces_endpoint(std::string_view agent_url) {
  // `agent_url` came from a validated configuration, so it is guaranteed to
  // parse successfully.
  auto url = std::get<HTTPClient::URL>(DatadogAgentConfig::parse(agent_url));
  url.path += traces_api_path;
  return url;
}

std::optional<Error> msgpack_encode(
    std::string& destination,
    const std::vector<std::unique_ptr<SpanData>>& spans) try {
  msgpack::pack_array(destination, spans.size());

  for (const auto& span_ptr : spans) {
    assert(span_ptr);
    if (auto maybe_error = msgpack_encode(destination, *span_ptr)) {
      return maybe_error;
    }
  }

  return std::nullopt;
} catch (const std::exception& error) {
  return Error{Error::MESSAGEPACK_ENCODE_FAILURE, error.what()};
}

std::optional<Error> msgpack_encode(
    std::string& destination,
    const std::vector<DatadogAgent::TraceChunk>& trace_chunks) try {
  msgpack::pack_array(destination, trace_chunks.size());

  for (const auto& chunk : trace_chunks) {
    if (auto maybe_error = msgpack_encode(destination, chunk.spans)) {
      return maybe_error;
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

  std::unordered_map<std::string, double> sample_rates;
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
    const double rate = value;
    if (!(rate >= 0 && rate <= 1)) {
      std::string message;
      message +=
          "Datadog Agent response to traces included an invalid sample rate "
          "for the key \"";
      message += key;
      message += "\". Rate should be between zero and one, but it's ";
      message += std::to_string(rate);
      message += " instead.";
      message += "\nError occurred for response body (begins on next line):\n";
      message += body;
      return message;
    }
    sample_rates.emplace(key, rate);
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

DatadogAgent::DatadogAgent(const Validated<DatadogAgentConfig>& config)
    : traces_endpoint_(traces_endpoint(config.agent_url)),
      http_client_(config.http_client),
      event_scheduler_(config.event_scheduler),
      cancel_scheduled_flush_(event_scheduler_->schedule_recurring_event(
          std::chrono::milliseconds(config.flush_interval_milliseconds),
          [this]() { flush(); })) {}

DatadogAgent::~DatadogAgent() { cancel_scheduled_flush_(); }

std::optional<Error> DatadogAgent::send(
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

  std::string body;
  if (const auto maybe_error = msgpack_encode(body, outgoing_trace_chunks_)) {
    // TODO: need a logger
    std::cout << *maybe_error << '\n';
    (void)maybe_error;
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
    headers.set("Datadog-Meta-Tracer-Version", "TODO");
    headers.set("X-Datadog-Trace-Count",
                std::to_string(outgoing_trace_chunks_.size()));
  };

  // This is the callback for the HTTP response.  It's invoked
  // asynchronously.
  auto on_response = [samplers = std::move(response_handlers)](
                         int response_status,
                         const DictReader& /*response_headers*/,
                         std::string response_body) {
    if (response_status < 200 || response_status >= 300) {
      // TODO: need a logger
      std::cout << "Unexpected response status " << response_status
                << " with body (starts on next line):\n"
                << response_body << '\n';
      return;
    }
    auto result = parse_agent_traces_response(response_body);
    if (const auto* error_message = std::get_if<std::string>(&result)) {
      // TODO: need a logger
      std::cout << *error_message << '\n';
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
  auto on_error = [](Error error) {
    // TODO: error handler
    std::cout << "Error occurred during HTTP request: " << error << '\n';
  };

  if (const auto maybe_error = http_client_->post(
          traces_endpoint_, std::move(set_request_headers), std::move(body),
          std::move(on_response), std::move(on_error))) {
    // TODO: need logger
    std::cout << *maybe_error << '\n';
  }
}

}  // namespace tracing
}  // namespace datadog
