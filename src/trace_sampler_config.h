#pragma once

#include <variant>

#include "error.h"
#include "expected.h"

namespace datadog {
namespace tracing {

struct TraceSamplerConfig {
  // TODO
};

class FinalizedTraceSamplerConfig {
  friend Expected<FinalizedTraceSamplerConfig> finalize_config(
      const TraceSamplerConfig& config);

  FinalizedTraceSamplerConfig() = default;

 public:
  // TODO
};

Expected<FinalizedTraceSamplerConfig> finalize_config(
    const TraceSamplerConfig& config);

}  // namespace tracing
}  // namespace datadog
