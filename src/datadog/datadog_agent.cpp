#include "datadog_agent.h"

#include <datadog/datadog_agent_config.h>
#include <datadog/dict_writer.h>
#include <datadog/http_client.h>
#include <datadog/logger.h>
#include <datadog/string_view.h>
#include <datadog/telemetry/telemetry.h>
#include <datadog/tracer.h>

#include <cassert>
#include <chrono>
#include <string>
#include <unordered_map>
#include <unordered_set>

#include "collector_response.h"
#include "json.hpp"
#include "msgpack.h"
#include "span_data.h"
#include "trace_sampler.h"

namespace datadog {
namespace tracing {
namespace {

constexpr StringView traces_api_path = "/v0.4/traces";
constexpr StringView remote_configuration_path = "/v0.7/config";

void set_content_type_json(DictWriter& headers) {
  headers.set("Content-Type", "application/json");
}

HTTPClient::URL traces_endpoint(const HTTPClient::URL& agent_url) {
  auto traces_url = agent_url;
  append(traces_url.path, traces_api_path);
  return traces_url;
}

HTTPClient::URL remote_configuration_endpoint(
    const HTTPClient::URL& agent_url) {
  auto remote_configuration = agent_url;
  append(remote_configuration.path, remote_configuration_path);
  return remote_configuration;
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

namespace rc = datadog::remote_config;

DatadogAgent::DatadogAgent(
    const FinalizedDatadogAgentConfig& config,
    const std::shared_ptr<Logger>& logger,
    const TracerSignature& tracer_signature,
    const std::vector<std::shared_ptr<rc::Listener>>& rc_listeners)
    : clock_(config.clock),
      logger_(logger),
      traces_endpoint_(traces_endpoint(config.url)),
      remote_configuration_endpoint_(remote_configuration_endpoint(config.url)),
      http_client_(config.http_client),
      event_scheduler_(config.event_scheduler),
      flush_interval_(config.flush_interval),
      request_timeout_(config.request_timeout),
      shutdown_timeout_(config.shutdown_timeout),
      remote_config_(tracer_signature, rc_listeners, logger),
      tracer_signature_(tracer_signature) {
  assert(logger_);

  tasks_.emplace_back(event_scheduler_->schedule_recurring_event(
      config.flush_interval, [this]() { flush(); }));

  if (config.remote_configuration_enabled) {
    tasks_.emplace_back(event_scheduler_->schedule_recurring_event(
        config.remote_configuration_poll_interval,
        [this] { get_and_apply_remote_configuration_updates(); }));
  }
}

DatadogAgent::~DatadogAgent() {
  const auto deadline = clock_().tick + shutdown_timeout_;

  for (auto&& cancel_task : tasks_) {
    cancel_task();
  }

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

std::string DatadogAgent::config() const {
  // clang-format off
  return nlohmann::json::object({
    {"type", "datadog::tracing::DatadogAgent"},
    {"config", nlohmann::json::object({
      {"traces_url", (traces_endpoint_.scheme + "://" + traces_endpoint_.authority + traces_endpoint_.path)},
      {"remote_configuration_url", (remote_configuration_endpoint_.scheme + "://" + remote_configuration_endpoint_.authority + remote_configuration_endpoint_.path)},
      {"flush_interval_milliseconds", std::chrono::duration_cast<std::chrono::milliseconds>(flush_interval_).count() },
      {"request_timeout_milliseconds", std::chrono::duration_cast<std::chrono::milliseconds>(request_timeout_).count() },
      {"shutdown_timeout_milliseconds", std::chrono::duration_cast<std::chrono::milliseconds>(shutdown_timeout_).count() },
      {"http_client", nlohmann::json::parse(http_client_->config())},
      {"event_scheduler", nlohmann::json::parse(event_scheduler_->config())},
    })},
  }).dump();
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
    headers.set("Datadog-Meta-Lang-Version",
                tracer_signature_.library_language_version);
    headers.set("Datadog-Meta-Tracer-Version",
                tracer_signature_.library_version);
    headers.set("X-Datadog-Trace-Count", std::to_string(trace_chunks.size()));
  };

  // This is the callback for the HTTP response.  It's invoked
  // asynchronously.
  auto on_response = [samplers = std::move(response_handlers),
                      logger = logger_](int response_status,
                                        const DictReader& /*response_headers*/,
                                        std::string response_body) {
    if (response_status >= 500) {
      telemetry::metrics().trace_api.responses_5xx.inc();
    } else if (response_status >= 400) {
      telemetry::metrics().trace_api.responses_4xx.inc();
    } else if (response_status >= 300) {
      telemetry::metrics().trace_api.responses_3xx.inc();
    } else if (response_status >= 200) {
      telemetry::metrics().trace_api.responses_2xx.inc();
    } else if (response_status >= 100) {
      telemetry::metrics().trace_api.responses_1xx.inc();
    }
    if (response_status != 200) {
      logger->log_error([&](auto& stream) {
        stream << "Unexpected response status " << response_status
               << " in Datadog Agent response with body of length "
               << response_body.size() << " (starts on next line):\n"
               << response_body;
      });
      return;
    }

    if (response_body.empty()) {
      logger->log_error([](auto& stream) {
        stream << "Datadog Agent returned response without a body."
                  " This tracer might be sending batches of traces too "
                  "frequently";
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
    telemetry::metrics().trace_api.errors_network.inc();
    logger->log_error(error.with_prefix(
        "Error occurred during HTTP request for submitting traces: "));
  };

  telemetry::metrics().trace_api.requests.inc();
  auto post_result =
      http_client_->post(traces_endpoint_, std::move(set_request_headers),
                         std::move(body), std::move(on_response),
                         std::move(on_error), clock_().tick + request_timeout_);
  if (auto* error = post_result.if_error()) {
    logger_->log_error(
        error->with_prefix("Unexpected error submitting traces: "));
  }
}

void DatadogAgent::get_and_apply_remote_configuration_updates() {
  auto remote_configuration_on_response =
      [this](int response_status, const DictReader& /*response_headers*/,
             std::string response_body) {
        if (response_status < 200 || response_status >= 300) {
          if (response_status == 404) {
            /*
             * 404 is not considered as an error as the agent use it to
             * signal remote configuration is disabled. At any point, the
             * feature could be enabled, so the tracer must continuously check
             * for new remote configuration.
             */
            return;
          }

          logger_->log_error([&](auto& stream) {
            stream << "Unexpected Remote Configuration status "
                   << response_status
                   << " with body (if any, starts on next line):\n"
                   << response_body;
          });

          return;
        }

        const auto response_json =
            nlohmann::json::parse(/* input = */ response_body,
                                  /* parser_callback = */ nullptr,
                                  /* allow_exceptions = */ false);
        if (response_json.is_discarded()) {
          logger_->log_error([](auto& stream) {
            stream << "Could not parse Remote Configuration response body";
          });
          return;
        }

        if (!response_json.empty()) {
          remote_config_.process_response(response_json);
          // NOTE(@dmehala): Not ideal but it mimics the old behavior.
          // In the future, I would prefer telemetry pushing to the agent
          // and not the agent pulling from telemetry. That way telemetry will
          // be more flexible and could support env var to customize how often
          // it captures metrics.
          telemetry::send_configuration_change();
        }
      };

  auto remote_configuration_on_error = [logger = logger_](Error error) {
    logger->log_error(error.with_prefix(
        "Error occurred during HTTP request for Remote Configuration: "));
  };

  auto post_result = http_client_->post(
      remote_configuration_endpoint_, set_content_type_json,
      remote_config_.make_request_payload().dump(),
      remote_configuration_on_response, remote_configuration_on_error,
      clock_().tick + request_timeout_);
  if (auto error = post_result.if_error()) {
    logger_->log_error(
        error->with_prefix("Unexpected error while requesting Remote "
                           "Configuration updates: "));
  }
}

}  // namespace tracing
}  // namespace datadog
