#pragma once

#include <mutex>

#include "trace_sampler_config.h"
#include "validated.h"

namespace datadog {
namespace tracing {

class TraceSampler {
  std::mutex mutex_;
  // TODO

 public:
  explicit TraceSampler(const Validated<TraceSamplerConfig>& config);
};

}  // namespace tracing
}  // namespace datadog
