#include "config_manager.h"

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

void ConfigManager::update(const ConfigUpdate& conf) {
  std::lock_guard<std::mutex> lock(mutex_);

  if (conf.trace_sampler) {
    if (auto finalized_trace_sampler_cfg =
            finalize_config(*conf.trace_sampler)) {
      current_trace_sampler_ =
          std::make_shared<TraceSampler>(*finalized_trace_sampler_cfg, clock_);
    } else {
      // TODO: report error
    }
  } else {
    current_trace_sampler_ = default_trace_sampler_;
  }
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
