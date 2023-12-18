#include "config_manager.h"

#include "environment.h"
#include "tracer_telemetry.h"

namespace datadog {
namespace tracing {

ConfigManager::ConfigManager(const FinalizedTracerConfig& config)
    : clock_(config.clock),
      default_trace_sampler_(
          std::make_shared<TraceSampler>(config.trace_sampler, clock_)),
      current_trace_sampler_(default_trace_sampler_) {}

std::shared_ptr<TraceSampler> ConfigManager::get_trace_sampler() {
  std::lock_guard<std::mutex> lock(mutex_);
  return current_trace_sampler_;
}

std::vector<ConfigTelemetry> ConfigManager::update(const ConfigUpdate& conf) {
  std::vector<ConfigTelemetry> res;

  std::lock_guard<std::mutex> lock(mutex_);

  if (conf.trace_sampler) {
    ConfigTelemetry cfg_telemetry;
    cfg_telemetry.name = std::string{name(environment::DD_TRACE_SAMPLE_RATE)};
    cfg_telemetry.value = std::to_string(*conf.trace_sampler->sample_rate);
    cfg_telemetry.origin = ConfigOrigin::REMOTE_CONFIG;

    auto finalized_trace_sampler_cfg = finalize_config(*conf.trace_sampler);
    if (auto error = finalized_trace_sampler_cfg.if_error()) {
      cfg_telemetry.error = std::move(*error);
    } else {
      current_trace_sampler_ =
          std::make_shared<TraceSampler>(*finalized_trace_sampler_cfg, clock_);
    }

    res.emplace_back(std::move(cfg_telemetry));
  } else {
    current_trace_sampler_ = default_trace_sampler_;
  }

  return res;
}

void ConfigManager::reset() {
  std::lock_guard<std::mutex> lock(mutex_);
  current_trace_sampler_ = default_trace_sampler_;
}

nlohmann::json ConfigManager::config_json() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return nlohmann::json{
      {"trace_sampler", current_trace_sampler_->config_json()}};
}

}  // namespace tracing
}  // namespace datadog
