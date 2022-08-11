#pragma once

#include <mutex>

#include "span_sampler_config.h"

namespace datadog {
namespace tracing {

class SpanSampler {
  std::mutex mutex_;
  // TODO
};

}  // namespace tracing
}  // namespace datadog
