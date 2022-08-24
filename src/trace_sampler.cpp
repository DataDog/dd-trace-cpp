#include "trace_sampler.h"

#include "collector_response.h"

namespace datadog {
namespace tracing {

TraceSampler::TraceSampler(const Validated<TraceSamplerConfig>& config) {
  // TODO
  (void)config;
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
