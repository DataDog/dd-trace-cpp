#include "config_manager.h"

#include "trace_sampler.h"

namespace datadog {
namespace tracing {

ConfigManager::ConfigManager(const FinalizedTracerConfig& config)
    : clock_(config.clock),
      default_trace_sampler_(
          std::make_shared<TraceSampler>(config.trace_sampler, clock_)),
      current_trace_sampler_(default_trace_sampler_),
      default_span_defaults_(std::make_shared<SpanDefaults>(config.defaults)),
      current_span_defaults_(default_span_defaults_) {}

std::shared_ptr<TraceSampler> ConfigManager::get_trace_sampler() {
  std::lock_guard<std::mutex> lock(mutex_);
  return current_trace_sampler_;
}

std::shared_ptr<const SpanDefaults> ConfigManager::get_span_defaults() {
  std::lock_guard<std::mutex> lock(mutex_);
  return current_span_defaults_;
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

  if (conf.tags) {
    auto new_span_defaults =
        std::make_shared<SpanDefaults>(*current_span_defaults_);
    new_span_defaults->tags = std::move(*conf.tags);

    current_span_defaults_ = new_span_defaults;
  } else {
    current_span_defaults_ = default_span_defaults_;
  }
}

void ConfigManager::reset() {
  std::lock_guard<std::mutex> lock(mutex_);
  current_trace_sampler_ = default_trace_sampler_;
  current_span_defaults_ = default_span_defaults_;
}

nlohmann::json ConfigManager::config_json() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return nlohmann::json{
      {"default", to_json(*current_span_defaults_)},
      {"trace_sampler", current_trace_sampler_->config_json()}};
}

}  // namespace tracing
}  // namespace datadog
