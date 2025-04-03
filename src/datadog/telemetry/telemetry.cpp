#include <datadog/clock.h>
#include <datadog/datadog_agent_config.h>
#include <datadog/dict_writer.h>
#include <datadog/runtime_id.h>
#include <datadog/string_view.h>
#include <datadog/telemetry/telemetry.h>

#include <chrono>

#include "datadog_agent.h"
#include "platform_util.h"
#include "tracer_telemetry.h"

using namespace datadog::tracing;
using namespace std::chrono_literals;

namespace datadog {
namespace telemetry {
namespace {

HTTPClient::URL make_telemetry_endpoint(HTTPClient::URL url) {
  append(url.path, "/telemetry/proxy/api/v2/apmtelemetry");
  return url;
}

}  // namespace
Telemetry::Telemetry(FinalizedConfiguration config,
                     std::shared_ptr<tracing::Logger> logger,
                     std::shared_ptr<tracing::HTTPClient> client,
                     std::vector<std::shared_ptr<Metric>> metrics,
                     tracing::EventScheduler& event_scheduler,
                     HTTPClient::URL agent_url, Clock clock)
    : config_(std::move(config)),
      logger_(std::move(logger)),
      telemetry_endpoint_(make_telemetry_endpoint(agent_url)),
      tracer_signature_(tracing::RuntimeID::generate(),
                        tracing::get_process_name(), ""),
      http_client_(client),
      clock_(std::move(clock)) {
  tracer_telemetry_ = std::make_shared<tracing::TracerTelemetry>(
      config_.enabled, clock_, logger_, tracer_signature_,
      config_.integration_name, config_.integration_version,
      std::vector<std::reference_wrapper<Metric>>{
          {metrics_.tracer.spans_created},
          {metrics_.tracer.spans_finished},
          {metrics_.tracer.trace_segments_created_new},
          {metrics_.tracer.trace_segments_created_continued},
          {metrics_.tracer.trace_segments_closed},
          {metrics_.trace_api.requests},
          {metrics_.trace_api.responses_1xx},
          {metrics_.trace_api.responses_2xx},
          {metrics_.trace_api.responses_3xx},
          {metrics_.trace_api.responses_4xx},
          {metrics_.trace_api.responses_5xx},
          {metrics_.trace_api.errors_timeout},
          {metrics_.trace_api.errors_network},
          {metrics_.trace_api.errors_status_code},
      },
      metrics);

  if (!config_.enabled) {
    return;
  }

  // Callback for successful telemetry HTTP requests, to examine HTTP
  // status.
  telemetry_on_response_ = [logger = logger_](
                               int response_status,
                               const DictReader& /*response_headers*/,
                               std::string response_body) {
    if (response_status < 200 || response_status >= 300) {
      logger->log_error([&](auto& stream) {
        stream << "Unexpected telemetry response status " << response_status
               << " with body (if any, starts on next line):\n"
               << response_body;
      });
    }
  };

  // Callback for unsuccessful telemetry HTTP requests.
  telemetry_on_error_ = [logger = logger_](Error error) {
    logger->log_error(error.with_prefix(
        "Error occurred during HTTP request for telemetry: "));
  };

  // Only schedule this if telemetry is enabled.
  // Every 10 seconds, have the tracer telemetry capture the metrics
  // values. Every 60 seconds, also report those values to the datadog
  // agent.
  tasks_.emplace_back(event_scheduler.schedule_recurring_event(
      std::chrono::seconds(10), [this, n = 0]() mutable {
        n++;
        tracer_telemetry_->capture_metrics();
        if (n % 6 == 0) {
          send_heartbeat_and_telemetry();
        }
      }));
}

Telemetry::~Telemetry() {
  if (!config_.enabled) return;
  tracer_telemetry_->capture_metrics();
  // The app-closing message is bundled with a message containing the
  // final metric values.
  send_app_closing();
  http_client_->drain(clock_().tick + 1s);
}

void Telemetry::log_error(std::string message) {
  tracer_telemetry_->log(std::move(message), LogLevel::ERROR);
}

void Telemetry::log_warning(std::string message) {
  tracer_telemetry_->log(std::move(message), LogLevel::WARNING);
}

void Telemetry::send_telemetry(StringView request_type, std::string payload) {
  if (!config_.enabled) return;
  auto set_telemetry_headers = [request_type, payload_size = payload.size(),
                                debug_enabled = tracer_telemetry_->debug(),
                                tracer_signature =
                                    &tracer_signature_](DictWriter& headers) {
    /*
      TODO:
        Datadog-Container-ID
    */
    headers.set("Content-Type", "application/json");
    headers.set("Content-Length", std::to_string(payload_size));
    headers.set("DD-Telemetry-API-Version", "v2");
    headers.set("DD-Client-Library-Language", "cpp");
    headers.set("DD-Client-Library-Version", tracer_signature->library_version);
    headers.set("DD-Telemetry-Request-Type", request_type);

    if (debug_enabled) {
      headers.set("DD-Telemetry-Debug-Enabled", "true");
    }
  };

  // TODO(@dmehala): make `clock::instance()` a singleton
  auto post_result = http_client_->post(
      telemetry_endpoint_, set_telemetry_headers, std::move(payload),
      telemetry_on_response_, telemetry_on_error_,
      tracing::default_clock().tick + 5s);
  if (auto* error = post_result.if_error()) {
    logger_->log_error(
        error->with_prefix("Unexpected error submitting telemetry event: "));
  }
}

void Telemetry::send_app_started(
    const std::unordered_map<ConfigName, ConfigMetadata>& config_metadata) {
  send_telemetry("app-started",
                 tracer_telemetry_->app_started(config_metadata));
}

void Telemetry::send_app_closing() {
  send_telemetry("app-closing", tracer_telemetry_->app_closing());
}

void Telemetry::send_heartbeat_and_telemetry() {
  send_telemetry("app-heartbeat", tracer_telemetry_->heartbeat_and_telemetry());
}

void Telemetry::send_configuration_change() {
  if (auto payload = tracer_telemetry_->configuration_change()) {
    send_telemetry("app-client-configuration-change", *payload);
  }
}

void Telemetry::capture_configuration_change(
    const std::vector<tracing::ConfigMetadata>& new_configuration) {
  return tracer_telemetry_->capture_configuration_change(new_configuration);
}

}  // namespace telemetry
}  // namespace datadog
