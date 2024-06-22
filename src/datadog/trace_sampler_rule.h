#pragma once

#include "datadog/span_matcher.h"
#include "json_fwd.hpp"
#include "rate.h"
#include "sampling_mechanism.h"

namespace datadog::tracing {

struct TraceSamplerRule final {
  Rate rate;
  SpanMatcher matcher;
  SamplingMechanism mechanism;

  nlohmann::json to_json() const;
};
}  // namespace datadog::tracing
