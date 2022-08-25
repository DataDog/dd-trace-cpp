#pragma once

#include <iosfwd>
#include <optional>

#include "sampling_mechanism.h"

namespace datadog {
namespace tracing {

struct SamplingDecision {
  enum class Origin { EXTRACTED, LOCAL, DELEGATED };

  bool keep;
  std::optional<SamplingMechanism> mechanism;
  Origin origin;
  bool awaiting_delegated_decision;

  // TODO: I'm using this during development.  Not sure
  // if it's worth keeping.  Maybe for error messages.
  void to_json(std::ostream&) const;
};

}  // namespace tracing
}  // namespace datadog
