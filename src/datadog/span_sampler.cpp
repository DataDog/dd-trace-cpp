#include "span_sampler.h"

#include "sampling_mechanism.h"
#include "sampling_priority.h"
#include "sampling_util.h"
#include "span_data.h"

namespace datadog {
namespace tracing {

SpanSampler::SynchronizedLimiter::SynchronizedLimiter(const Clock& clock,
                                                      double max_per_second)
    : limiter(clock, max_per_second) {}

SpanSampler::Rule::Rule(const FinalizedSpanSamplerConfig::Rule& rule,
                        const Clock& clock)
    : FinalizedSpanSamplerConfig::Rule(rule),
      limiter_(max_per_second ? std::make_unique<SynchronizedLimiter>(
                                    clock, *max_per_second)
                              : nullptr) {}

SamplingDecision SpanSampler::Rule::decide(const SpanData& span) {
  SamplingDecision decision;
  decision.mechanism = int(SamplingMechanism::SPAN_RULE);
  decision.origin = SamplingDecision::Origin::LOCAL;
  decision.configured_rate = sample_rate;
  decision.limiter_max_per_second = max_per_second;

  const std::uint64_t threshold = max_id_from_rate(sample_rate);
  if (knuth_hash(span.span_id) >= threshold) {
    decision.priority = int(SamplingPriority::USER_DROP);
    return decision;
  }

  if (!limiter_) {
    decision.priority = int(SamplingPriority::USER_KEEP);
    return decision;
  }

  std::lock_guard<std::mutex> lock(limiter_->mutex);
  const auto result = limiter_->limiter.allow();
  if (result.allowed) {
    decision.priority = int(SamplingPriority::USER_KEEP);
  } else {
    decision.priority = int(SamplingPriority::USER_DROP);
  }
  decision.limiter_effective_rate = result.effective_rate;

  return decision;
}

SpanSampler::SpanSampler(const FinalizedSpanSamplerConfig& config,
                         const Clock& clock) {
  for (const auto& rule : config.rules) {
    rules_.push_back(Rule{rule, clock});
  }
}

SpanSampler::Rule* SpanSampler::match(const SpanData& span) {
  // TODO
  (void)span;
  return nullptr;
}

}  // namespace tracing
}  // namespace datadog
