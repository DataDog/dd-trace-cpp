#include <datadog/clock.h>
#include <datadog/datadog_agent_config.h>
#include <datadog/dict_writer.h>
#include <datadog/runtime_id.h>
#include <datadog/telemetry/telemetry.h>

#include <cassert>

#include "batch.h"
#include "datadog_agent.h"
#include "json_serializer.h"
#include "platform_util.h"
#include "tracer_telemetry.h"

namespace datadog {
namespace telemetry {

Batch batch;

Telemetry::Telemetry(FinalizedConfiguration config,
                     std::shared_ptr<tracing::EventScheduler> scheduler,
                     std::shared_ptr<tracing::HTTPClient> http_client,
                     std::shared_ptr<tracing::Logger> logger,
                     std::vector<std::shared_ptr<Metric>> metrics)
    : config_(std::move(config)),
      logger_(std::move(logger)),
      http_client_(std::move(http_client)) {
  assert(scheduler);
  if (!config_.enabled) {
    return;
  }

  tracing::TracerSignature tracer_signature(tracing::RuntimeID::generate(),
                                            tracing::get_process_name(), "");

  tracer_telemetry_ = std::make_shared<tracing::TracerTelemetry>(
      config_.enabled, tracing::default_clock, logger_, tracer_signature,
      config_.integration_name, config_.integration_version, metrics);

  batch.add_event(EventType::app_started);
  flush();

  tasks_.emplace_back(
      scheduler->schedule_recurring_event(config_.heartbeat_interval, [this] {
        // NOTE(@dmehala): Every flush should contain the `app-heartbeat`
        batch.add_event(EventType::app_heartbeat);
        flush();
      }));

  if (config_.report_metrics) {
    tasks_.emplace_back(
        scheduler->schedule_recurring_event(config_.metrics_interval, [] {}));
  }
}

Telemetry::~Telemetry() {
  for (auto& cancel_task : tasks_) {
    cancel_task();
  }

  batch.add_event(EventType::app_closing);
  // TODO: flush send ap_heartbeat by default. change for app_closing
  flush();
}

Serializer<JsonSerializer> serializer_;

void Telemetry::flush() {
  Batch current_batch;
  std::swap(batch, current_batch);

  // NOTE(@dmehala): we don't want make an allocation everytime
  // but reuse the same buffer accross the entire lifecycle of the telemetry
  // module.
  serializer_(current_batch);
  const std::string& payload = serializer_.get_buffer();

  auto set_headers = [debug_enabled = tracer_telemetry_->debug()](
                         tracing::DictWriter& headers) {
    headers.set("Content-Type", "application/json");
    headers.set("DD-Telemetry-API-Version", "v2");
    headers.set("DD-Client-Library-Language", "cpp");
    /*headers.set("DD-Client-Library-Version",
     * tracer_signature->library_version);*/
    headers.set("DD-Telemetry-Request-Type",
                to_string(EventType::app_heartbeat));

    if (debug_enabled) {
      headers.set("DD-Telemetry-Debug-Enabled", "true");
    }
  };

  auto on_error = [logger = logger_](tracing::Error error) {
    logger->log_error(error.with_prefix(
        "Error occurred during HTTP request for telemetry: "));
  };

  auto maybe_post = http_client_->post(
      "/telemetry/proxy/api/v2/apmtelemetry", set_headers, std::move(payload),
      on_error, clock_().tick + config_.request_timeout_);
  if (auto* error = maybe_post.if_error()) {
    logger_->log_error(
        error->with_prefix("Unexpected error submitting telemetry event: "));
  }
}

}  // namespace telemetry
}  // namespace datadog
