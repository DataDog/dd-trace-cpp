#pragma once

// This component provides a `struct`, `TraceSamplerConfig`, used to configure
// `TraceSampler`. `TraceSampler` accepts a `FinalizedTraceSamplerConfig`, which
// must be obtained from a call to `finalize_config`.
//
// `TraceSamplerConfig` is specified as the `trace_sampler` property of
// `TracerConfig`.

#include <vector>

#include "expected.h"
#include "json_fwd.hpp"
#include "optional.h"
#include "rate.h"
#include "span_matcher.h"

namespace datadog {
namespace tracing {

struct TraceSamplerConfig {
  struct Rule : public SpanMatcher {
    double sample_rate = 1.0;

    Rule(const SpanMatcher&);
    Rule() = default;
  };

  Optional<double> sample_rate;
  std::vector<Rule> rules;
  double max_per_second = 200;
};

class FinalizedTraceSamplerConfig {
  friend Expected<FinalizedTraceSamplerConfig> finalize_config(
      const TraceSamplerConfig& config);
  friend class FinalizedTracerConfig;

  FinalizedTraceSamplerConfig() = default;

 public:
  struct Rule : public SpanMatcher {
    Rate sample_rate;
  };

  std::vector<Rule> rules;
  double max_per_second;
};

Expected<FinalizedTraceSamplerConfig> finalize_config(
    const TraceSamplerConfig& config);

nlohmann::json to_json(const FinalizedTraceSamplerConfig::Rule&);

}  // namespace tracing
}  // namespace datadog
