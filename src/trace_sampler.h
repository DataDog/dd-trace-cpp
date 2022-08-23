#pragma once

#include <mutex>
#include <string>
#include <unordered_map>

#include "trace_sampler_config.h"
#include "validated.h"

namespace datadog {
namespace tracing {

class CollectorResponse;

class TraceSampler {
  std::mutex mutex_;
  double collector_default_sample_rate_;
  std::unordered_map<std::string, double> collector_sample_rates_;

 public:
  explicit TraceSampler(const Validated<TraceSamplerConfig>& config);

  void handle_collector_response(CollectorResponse);
};

}  // namespace tracing
}  // namespace datadog
