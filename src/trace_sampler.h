#pragma once

#include <mutex>

#include "trace_sampler_config.h"

namespace datadog {
namespace tracing {

class TraceSampler {
  std::mutex mutex_;
  // TODO
};

}  // namespace tracing
}  // namespace datadog
