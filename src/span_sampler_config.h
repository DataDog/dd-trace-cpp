#pragma once

#include <variant>

#include "validated.h"

namespace datadog {
namespace tracing {

struct SpanSamplerConfig {
  // TODO
};

std::variant<Validated<SpanSamplerConfig>, Error> validate_config(
    const SpanSamplerConfig& config);

}  // namespace tracing
}  // namespace datadog
