#pragma once

// The `ConfigManager` class is designed to handle configuration update
// and provide access to the current configuration.
// It utilizes a mutex to ensure thread safety when updating or accessing
// the configuration.

#include <mutex>

#include "clock.h"
#include "config_update.h"
#include "json.hpp"
#include "tracer_config.h"

namespace datadog {
namespace tracing {

class ConfigManager {
  mutable std::mutex mutex_;
  Clock clock_;
  std::shared_ptr<TraceSampler> default_trace_sampler_;
  std::shared_ptr<TraceSampler> current_trace_sampler_;

 public:
  ConfigManager(const FinalizedTracerConfig& config);

  // Return the `TraceSampler` consistent with the most recent configuration.
  std::shared_ptr<TraceSampler> get_trace_sampler();

  // Apply the specified `conf` update.
  void update(const ConfigUpdate& conf);

  // Restore the configuration that was passed to this object's constructor,
  // overriding any previous calls to `update`.
  void reset();

  // Return a JSON representation of the current configuration managed by this
  // object.
  nlohmann::json config_json() const;
};

}  // namespace tracing
}  // namespace datadog
