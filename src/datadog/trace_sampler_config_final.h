#pragma once

#include "config.h"
#include "datadog/expected.h"
#include "datadog/trace_sampler_config.h"
#include "trace_sampler_rule.h"

namespace datadog::tracing {

class FinalizedTraceSamplerConfig {
  friend Expected<FinalizedTraceSamplerConfig> finalize_config(
      const TraceSamplerConfig &config);
  friend class FinalizedTracerConfig;

  FinalizedTraceSamplerConfig() = default;

 public:
  double max_per_second;
  std::vector<TraceSamplerRule> rules;
  std::unordered_map<ConfigName, ConfigMetadata> metadata;
};
}  // namespace datadog::tracing
