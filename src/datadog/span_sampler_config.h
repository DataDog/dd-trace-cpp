#pragma once

#include <optional>
#include <vector>

#include "expected.h"
#include "rate.h"
#include "span_matcher.h"

namespace datadog {
namespace tracing {

class Logger;

struct SpanSamplerConfig {
  struct Rule : public SpanMatcher {
    double sample_rate = 1.0;
    std::optional<double> max_per_second;
  };

  std::vector<Rule> rules;
};

class FinalizedSpanSamplerConfig {
  friend Expected<FinalizedSpanSamplerConfig> finalize_config(
      const SpanSamplerConfig&, Logger&);
  friend class FinalizedTracerConfig;

  FinalizedSpanSamplerConfig() = default;

 public:
  struct Rule : public SpanMatcher {
    Rate sample_rate;
    std::optional<double> max_per_second;
  };

  std::vector<Rule> rules;
};

Expected<FinalizedSpanSamplerConfig> finalize_config(const SpanSamplerConfig&,
                                                     Logger&);

}  // namespace tracing
}  // namespace datadog
