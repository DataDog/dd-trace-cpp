#pragma once

// The `ConfigManager` class is designed to handle configuration update
// and provide access to the current configuration.
// It utilizes a mutex to ensure thread safety when updating or accessing
// the configuration.

#include <mutex>

#include "clock.h"
#include "config_update.h"
#include "json.hpp"
#include "span_defaults.h"
#include "tracer_config.h"

namespace datadog {
namespace tracing {

class ConfigManager {
  template <typename T>
  class DynamicConf {
    T default_value_;
    T current_value_;
    bool is_default_value_;

   public:
    explicit DynamicConf(T v)
        : default_value_(v), current_value_(v), is_default_value_(true) {}

    void update(T new_v) {
      current_value_ = new_v;
      is_default_value_ = false;
    }

    void reset() {
      current_value_ = default_value_;
      is_default_value_ = true;
    }

    bool is_default() const { return is_default_value_; }

    T get() { return current_value_; }
    const T& get() const { return current_value_; }

    operator T() const { return current_value_; }

    void operator=(const T& rhs) { update(rhs); }
  };

  mutable std::mutex mutex_;
  Clock clock_;
  std::unordered_map<ConfigName, ConfigMetadata> default_config_metadata_;

  DynamicConf<std::shared_ptr<TraceSampler>> trace_sampler_;
  DynamicConf<std::shared_ptr<const SpanDefaults>> span_defaults_;
  DynamicConf<bool> report_traces_;

 public:
  ConfigManager(const FinalizedTracerConfig& config);

  // Return the `TraceSampler` consistent with the most recent configuration.
  std::shared_ptr<TraceSampler> trace_sampler();

  // Return the `SpanDefaults` consistent with the most recent configuration.
  std::shared_ptr<const SpanDefaults> span_defaults();

  // Return whether traces should be sent to the collector.
  bool report_traces();

  // Apply the specified `conf` update.
  std::vector<ConfigMetadata> update(const ConfigUpdate& conf);

  // Restore the configuration that was passed to this object's constructor,
  // overriding any previous calls to `update`.
  std::vector<ConfigMetadata> reset();

  // Return a JSON representation of the current configuration managed by this
  // object.
  nlohmann::json config_json() const;
};

}  // namespace tracing
}  // namespace datadog
