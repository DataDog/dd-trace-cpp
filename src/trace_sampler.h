#pragma once

#include <mutex>
#include <optional>
#include <string>
#include <unordered_map>

#include "rate.h"
#include "trace_sampler_config.h"

namespace datadog {
namespace tracing {

class CollectorResponse;
struct SamplingDecision;

class TraceSampler {
  mutable std::mutex mutex_;
  std::optional<Rate> collector_default_sample_rate_;
  std::unordered_map<std::string, Rate> collector_sample_rates_;
  // TODO: sampling rules

 public:
  explicit TraceSampler(const FinalizedTraceSamplerConfig& config);

  SamplingDecision decide(std::uint64_t trace_id, std::string_view service,
                          std::string_view operation_name,
                          std::string_view environment) const;

  void handle_collector_response(const CollectorResponse&);
};

}  // namespace tracing
}  // namespace datadog
