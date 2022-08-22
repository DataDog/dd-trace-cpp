#pragma once

#include <variant>

#include "error.h"
#include "validated.h"

namespace datadog {
namespace tracing {

struct TraceSamplerConfig {
  // TODO
};

std::variant<Validated<TraceSamplerConfig>, Error> validate_config(
    const TraceSamplerConfig& config);

}  // namespace tracing
}  // namespace datadog
