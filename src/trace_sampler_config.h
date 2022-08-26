#pragma once

#include <variant>

#include "error.h"
#include "expected.h"
#include "validated.h"

namespace datadog {
namespace tracing {

struct TraceSamplerConfig {
  // TODO
};

Expected<Validated<TraceSamplerConfig>> validate_config(
    const TraceSamplerConfig& config);

}  // namespace tracing
}  // namespace datadog
