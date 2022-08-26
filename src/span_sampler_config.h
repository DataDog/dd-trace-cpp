#pragma once

#include <variant>

#include "expected.h"
#include "validated.h"

namespace datadog {
namespace tracing {

struct SpanSamplerConfig {
  // TODO
};

Expected<Validated<SpanSamplerConfig>> validate_config(
    const SpanSamplerConfig& config);

}  // namespace tracing
}  // namespace datadog
