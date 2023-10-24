#pragma once

#include <mutex>

#include "clock.h"
#include "json.hpp"
#include "trace_sampler.h"
#include "tracer_config.h"

namespace datadog {
namespace tracing {

struct ConfigUpdate {
  Optional<TraceSamplerConfig> trace_sampler;
};

class ConfigManager {
  mutable std::mutex mutex_;
  Clock clock_;
  std::shared_ptr<TraceSampler> default_trace_sampler_;
  std::shared_ptr<TraceSampler> current_trace_sampler_;

 public:
  ConfigManager(const FinalizedTracerConfig& config)
      : clock_(config.clock),
        default_trace_sampler_(
            std::make_shared<TraceSampler>(config.trace_sampler, clock_)),
        current_trace_sampler_(default_trace_sampler_) {}

  std::shared_ptr<TraceSampler> get_trace_sampler() {
    std::lock_guard<std::mutex> lock(mutex_);
    return current_trace_sampler_;
  }

  void update(const ConfigUpdate& conf) {
    std::lock_guard<std::mutex> lock(mutex_);

    if (conf.trace_sampler) {
      if (auto finalized_trace_sampler_cfg =
              finalize_config(*conf.trace_sampler)) {
        current_trace_sampler_ = std::make_shared<TraceSampler>(
            *finalized_trace_sampler_cfg, clock_);
      } else {
        // TODO: report error
      }
    } else {
      current_trace_sampler_ = default_trace_sampler_;
    }
  }

  void reset() {
    std::lock_guard<std::mutex> lock(mutex_);
    current_trace_sampler_ = default_trace_sampler_;
  }

  nlohmann::json config_json() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return nlohmann::json{
        {"trace_sampler", current_trace_sampler_->config_json()}};
  }
};

}  // namespace tracing
}  // namespace datadog
