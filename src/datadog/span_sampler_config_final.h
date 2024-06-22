#pragma once

#include "config.h"
#include "datadog/span_sampler_config.h"
#include "rate.h"

namespace datadog::tracing {

class FinalizedSpanSamplerConfig {
  friend Expected<FinalizedSpanSamplerConfig> finalize_config(
      const SpanSamplerConfig &, Logger &);
  friend class FinalizedTracerConfig;

  FinalizedSpanSamplerConfig() = default;

 public:
  struct Rule : public SpanMatcher {
    Rate sample_rate;
    Optional<double> max_per_second;
  };

  std::vector<Rule> rules;
  std::unordered_map<ConfigName, ConfigMetadata> metadata;
};
}  // namespace datadog::tracing
