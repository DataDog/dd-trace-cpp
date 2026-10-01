#pragma once

// The `SpanSamplerConfig` struct is used to configure `SpanSampler`.
// `SpanSampler` accepts a `FinalizedSpanSamplerConfig`, which must be obtained
// from a call to `finalize_config()`.
//
// `SpanSamplerConfig` is specified as the `span_sampler` property of
// `TracerConfig`.

#include <unordered_map>
#include <vector>

#include "config.h"
#include "expected.h"
#include "logger.h"
#include "optional.h"
#include "rate.h"
#include "span_matcher.h"

namespace datadog {
namespace tracing {

class StableConfig;

struct SpanSamplerConfig {
  struct Rule : public SpanMatcher {
    double sample_rate = 1.0;
    Optional<double> max_per_second;

    Rule(const SpanMatcher&);
    Rule() = default;
  };

  // Can be overriden by the `DD_TRACE_SAMPLING_RULES` environment variable.
  // Also, the `DD_TRACE_SAMPLE_RATE` environment variable, if present, causes a
  // corresponding `Rule` to be appended to `rules`.
  std::vector<Rule> rules;
  bool rules_configured = false;

  void clear_rules() {
    rules.clear();
    rules_configured = true;
  }
};

class FinalizedSpanSamplerConfig {
  friend Expected<FinalizedSpanSamplerConfig> finalize_config(
      const SpanSamplerConfig&, Logger&);
  friend Expected<FinalizedSpanSamplerConfig> finalize_config(
      const SpanSamplerConfig&, Logger&, const StableConfig*);
  friend class FinalizedTracerConfig;

  FinalizedSpanSamplerConfig() = default;

 public:
  struct Rule : public SpanMatcher {
    Rate sample_rate;
    Optional<double> max_per_second;
  };

  std::vector<Rule> rules;
  std::unordered_map<ConfigName, std::vector<ConfigMetadata>> metadata;
};

Expected<FinalizedSpanSamplerConfig> finalize_config(const SpanSamplerConfig&,
                                                     Logger&);
Expected<FinalizedSpanSamplerConfig> finalize_config(const SpanSamplerConfig&,
                                                     Logger&,
                                                     const StableConfig*);

std::string to_string(const FinalizedSpanSamplerConfig::Rule&);

}  // namespace tracing
}  // namespace datadog
