#pragma once

#include <optional>
#include <vector>

#include "error.h"
#include "expected.h"
#include "rate.h"
#include "span_matcher.h"

namespace datadog {
namespace tracing {

struct TraceSamplerConfig {
  struct Rule {
    SpanMatcher root_span;
    double sample_rate = 1.0;
  };

  std::optional<double> sample_rate;
  std::vector<Rule> rules;
  double max_per_second = 200;
};

class FinalizedTraceSamplerConfig {
  friend Expected<FinalizedTraceSamplerConfig> finalize_config(
      const TraceSamplerConfig& config);
  friend class FinalizedTracerConfig;

  FinalizedTraceSamplerConfig() = default;

 public:
  struct Rule {
    SpanMatcher root_span;
    Rate sample_rate;
  };

  std::vector<Rule> rules;
  double max_per_second;
};

Expected<FinalizedTraceSamplerConfig> finalize_config(
    const TraceSamplerConfig& config);

}  // namespace tracing
}  // namespace datadog
