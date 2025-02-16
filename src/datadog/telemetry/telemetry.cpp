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
                     std::shared_ptr<tracing::Logger> logger,
                     std::vector<std::shared_ptr<Metric>> metrics)
    : config_(std::move(config)), logger_(std::move(logger)) {
  if (!config_.enabled) {
    return;
  }

  tracing::TracerSignature tracer_signature(tracing::RuntimeID::generate(),
                                            tracing::get_process_name(), "");

  tracer_telemetry_ = std::make_shared<tracing::TracerTelemetry>(
      config_.enabled, tracing::default_clock, logger_, tracer_signature,
      config_.integration_name, config_.integration_version, metrics);

  tracing::DatadogAgentConfig dd_config;
  dd_config.remote_configuration_enabled = false;

  auto final_cfg =
      tracing::finalize_config(dd_config, logger_, tracing::default_clock);
  if (!final_cfg) {
    return;
  }

  datadog_agent_ = std::make_shared<tracing::DatadogAgent>(
      *final_cfg, tracer_telemetry_, logger_, tracer_signature,
      std::vector<std::shared_ptr<remote_config::Listener>>{});
}

void Telemetry::log_error(std::string message) {
  tracer_telemetry_->log(std::move(message), LogLevel::ERROR);
}

void Telemetry::log_warning(std::string message) {
  tracer_telemetry_->log(std::move(message), LogLevel::WARNING);
}

}  // namespace telemetry
}  // namespace datadog
