#pragma once

#include <iosfwd>
#include <optional>

#include "rate.h"
#include "sampling_mechanism.h"

namespace datadog {
namespace tracing {

struct SamplingDecision {
  enum class Origin { EXTRACTED, LOCAL, DELEGATED };

  int priority;
  std::optional<int> mechanism;
  std::optional<Rate> configured_rate;
  std::optional<Rate> limiter_effective_rate;
  std::optional<double> limiter_max_per_second;
  Origin origin;

  void to_json(std::ostream&) const;
};

}  // namespace tracing
}  // namespace datadog
