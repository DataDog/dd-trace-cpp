#pragma once

#include <iosfwd>
#include <optional>

#include "sampling_mechanism.h"

namespace datadog {
namespace tracing {

struct SamplingDecision {
  enum class Origin { EXTRACTED, LOCAL, DELEGATED };

  int priority;
  std::optional<int> mechanism;
  Origin origin;

  // TODO: I'm using this during development.  Not sure
  // if it's worth keeping.  Maybe for error messages.
  void to_json(std::ostream&) const;
};

}  // namespace tracing
}  // namespace datadog
