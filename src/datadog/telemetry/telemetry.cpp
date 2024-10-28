#include <datadog/clock.h>
#include <datadog/datadog_agent_config.h>
#include <datadog/runtime_id.h>
#include <datadog/telemetry/telemetry.h>

#include "datadog_agent.h"
#include "platform_util.h"
#include "tracer_telemetry.h"

namespace datadog {
namespace telemetry {

Telemetry::Telemetry(FinalizedConfiguration config,
                     std::shared_ptr<tracing::EventScheduler> scheduler,
                     std::shared_ptr<tracing::HTTPClient> http_client,
                     std::shared_ptr<tracing::Logger> logger,
                     std::vector<std::shared_ptr<Metric>> metrics)
    : config_(std::move(config)),
      http_client_(std::move(http_client)),
      logger_(std::move(logger)) {
  if (!config_.enabled) {
    return;
  }

  tracing::TracerSignature tracer_signature(tracing::RuntimeID::generate(),
                                            tracing::get_process_name(), "");

  tracer_telemetry_ = std::make_shared<tracing::TracerTelemetry>(
      config.enabled, tracing::default_clock, logger, tracer_signature,
      config.integration_name, config.integration_version, metrics);

  tracing::DatadogAgentConfig dd_config;
  dd_config.http_client = http_client_;
  dd_config.event_scheduler = scheduler;
  dd_config.remote_configuration_enabled = false;

  auto final_cfg =
      tracing::finalize_config(dd_config, logger, tracing::default_clock);
  if (!final_cfg) {
    return;
  }

  datadog_agent_ = std::make_shared<tracing::DatadogAgent>(
      *final_cfg, tracer_telemetry_, logger, tracer_signature,
      std::vector<std::shared_ptr<remote_config::Listener>>{});
}

}  // namespace telemetry
}  // namespace datadog
