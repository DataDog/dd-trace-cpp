#include "trace_sampler.h"

#include <algorithm>
#include <cassert>
#include <cstdint>
#include <limits>

#include "collector_response.h"
#include "sampling_decision.h"
#include "sampling_priority.h"
#include "span_data.h"

namespace datadog {
namespace tracing {
namespace {

std::uint64_t knuth_hash(std::uint64_t value) {
  return value * UINT64_C(1111111111111111111);
}

std::uint64_t max_id_from_rate(Rate rate) {
  // `double(std::numeric_limits<uint64_t>::max())` is slightly larger than the
  // largest `uint64_t`, but consider it a fun fact that the largest `double`
  // less than 1.0 (i.e. the "previous value" to 1.0), when multiplied by the
  // max `uint64_t`, results in a number not greater than the max `uint64_t`.
  // So, the only special case to consider is 1.0.
  if (rate == 1.0) {
    return std::numeric_limits<uint64_t>::max();
  }

  return rate * static_cast<double>(std::numeric_limits<std::uint64_t>::max());
}

}  // namespace

TraceSampler::TraceSampler(const FinalizedTraceSamplerConfig& config,
                           const Clock& clock)
    : rules_(config.rules), limiter_(clock, config.max_per_second) {}

SamplingDecision TraceSampler::decide(const SpanData& span) {
  SamplingDecision decision;
  decision.origin = SamplingDecision::Origin::LOCAL;

  // First check sampling rules.
  auto found_rule =
      std::find_if(rules_.begin(), rules_.end(),
                   [&](const auto& rule) { return rule.match(span); });

  if (found_rule != rules_.end()) {
    const auto& rule = *found_rule;
    decision.mechanism = int(SamplingMechanism::RULE);
    decision.configured_rate = rule.sample_rate;
    const std::uint64_t threshold = max_id_from_rate(rule.sample_rate);
    if (knuth_hash(span.trace_id) < threshold) {
      const auto result = limiter_.allow();
      if (result.allowed) {
        decision.priority = int(SamplingPriority::USER_KEEP);
      } else {
        decision.priority = int(SamplingPriority::USER_DROP);
      }
      decision.limiter_effective_rate = result.effective_rate;
    } else {
      decision.priority = int(SamplingPriority::USER_DROP);
    }

    return decision;
  }

  // No sampling rule matched.  Find the appropriate collector-controlled
  // sample rate.
  std::lock_guard lock(mutex_);
  auto found_rate = collector_sample_rates_.find(
      CollectorResponse::key(span.service, span.environment().value_or("")));
  if (found_rate != collector_sample_rates_.end()) {
    decision.configured_rate = found_rate->second;
    decision.mechanism = int(SamplingMechanism::AGENT_RATE);
  } else {
    if (collector_default_sample_rate_) {
      decision.configured_rate = *collector_default_sample_rate_;
      decision.mechanism = int(SamplingMechanism::AGENT_RATE);
    } else {
      // We have yet to receive a default rate from the collector.  This
      // corresponds to the `DEFAULT` sampling mechanism.
      decision.configured_rate = Rate::one();
      decision.mechanism = int(SamplingMechanism::DEFAULT);
    }
  }

  const std::uint64_t threshold = max_id_from_rate(*decision.configured_rate);
  if (knuth_hash(span.trace_id) < threshold) {
    decision.priority = int(SamplingPriority::AUTO_KEEP);
  } else {
    decision.priority = int(SamplingPriority::AUTO_DROP);
  }

  return decision;
}

void TraceSampler::handle_collector_response(
    const CollectorResponse& response) {
  const auto found =
      response.sample_rate_by_key.find(response.key_of_default_rate);
  std::lock_guard<std::mutex> lock(mutex_);

  if (found != response.sample_rate_by_key.end()) {
    collector_default_sample_rate_ = found->second;
  }

  collector_sample_rates_ = response.sample_rate_by_key;
}

}  // namespace tracing
}  // namespace datadog
