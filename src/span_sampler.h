#pragma once

#include <mutex>

#include "span_sampler_config.h"
#include "validated.h"

namespace datadog {
namespace tracing {

class SpanSampler {
  std::mutex mutex_;

 public:
  explicit SpanSampler(const Validated<SpanSamplerConfig>& config);
};

}  // namespace tracing
}  // namespace datadog
