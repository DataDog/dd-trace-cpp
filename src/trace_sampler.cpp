#include "trace_sampler.h"

#include <cassert>
#include <cstdint>
#include <limits>

#include "collector_response.h"
#include "sampling_decision.h"
#include "sampling_priority.h"

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

TraceSampler::TraceSampler(const FinalizedTraceSamplerConfig& config) {
  // TODO
  (void)config;
}

SamplingDecision TraceSampler::decide(std::uint64_t trace_id,
                                      std::string_view service,
                                      std::string_view /*operation_name*/,
                                      std::string_view environment) const {
  std::lock_guard lock(mutex_);
  // TODO: sampling rules

  // Find the appropriate rate and mechanism.
  Rate rate;
  SamplingMechanism mechanism;
  auto found = collector_sample_rates_.find(
      CollectorResponse::key(service, environment));
  if (found != collector_sample_rates_.end()) {
    rate = found->second;
    mechanism = SamplingMechanism::AGENT_RATE;
  } else {
    if (collector_default_sample_rate_) {
      rate = *collector_default_sample_rate_;
      mechanism = SamplingMechanism::AGENT_RATE;
    } else {
      // We have yet to receive a default rate from the collector.  This
      // corresponds to the `DEFAULT` sampling mechanism.
      rate = Rate::one();
      mechanism = SamplingMechanism::DEFAULT;
    }
  }

  const std::uint64_t threshold = max_id_from_rate(rate);
  SamplingDecision decision;
  if (knuth_hash(trace_id) < threshold) {
    decision.priority = int(SamplingPriority::AUTO_KEEP);
  } else {
    decision.priority = int(SamplingPriority::AUTO_DROP);
  }
  decision.mechanism = int(mechanism);
  decision.origin = SamplingDecision::Origin::LOCAL;

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
