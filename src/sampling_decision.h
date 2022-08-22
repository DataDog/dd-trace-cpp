#pragma once

#include "sampling_mechanism.h"

namespace datadog {
namespace tracing {

struct SamplingDecision {
  enum class Origin { EXTRACTED, LOCAL, DELEGATED };

  bool keep;
  SamplingMechanism mechanism;
  Origin origin;
};

}  // namespace tracing
}  // namespace datadog
