#pragma once

#include <mutex>
#include <optional>
#include <string>
#include <unordered_map>

#include "clock.h"
#include "limiter.h"
#include "rate.h"
#include "trace_sampler_config.h"

namespace datadog {
namespace tracing {

class CollectorResponse;
struct SamplingDecision;
struct SpanData;

class TraceSampler {
  std::mutex mutex_;

  std::optional<Rate> collector_default_sample_rate_;
  std::unordered_map<std::string, Rate> collector_sample_rates_;

  std::vector<FinalizedTraceSamplerConfig::Rule> rules_;
  Limiter limiter_;
  double limiter_max_per_second_;

 public:
  TraceSampler(const FinalizedTraceSamplerConfig& config, const Clock& clock);

  SamplingDecision decide(const SpanData&);

  void handle_collector_response(const CollectorResponse&);
};

}  // namespace tracing
}  // namespace datadog
