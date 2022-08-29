#pragma once

#include <memory>
#include <mutex>

#include "clock.h"
#include "limiter.h"
#include "sampling_decision.h"
#include "span_sampler_config.h"

namespace datadog {
namespace tracing {

class SpanSampler {
 public:
  struct SynchronizedLimiter {
    std::mutex mutex;
    Limiter limiter;

    SynchronizedLimiter(const Clock&, double max_per_second);
  };

  class Rule : public FinalizedSpanSamplerConfig::Rule {
    std::unique_ptr<SynchronizedLimiter> limiter_;

   public:
    explicit Rule(const FinalizedSpanSamplerConfig::Rule&, const Clock&);

    SamplingDecision decide(const SpanData&);
  };

 private:
  std::vector<Rule> rules_;

 public:
  explicit SpanSampler(const FinalizedSpanSamplerConfig& config,
                       const Clock& clock);

  Rule* match(const SpanData&);
};

}  // namespace tracing
}  // namespace datadog
