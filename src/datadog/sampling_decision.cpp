#include "sampling_decision.h"

#include <cassert>

namespace datadog {
namespace tracing {

std::string to_string(SamplingDecision::Origin origin) {
  switch (origin) {
    case SamplingDecision::Origin::EXTRACTED:
      return "extracted";
    case SamplingDecision::Origin::LOCAL:
      return "local";
    default:
      assert(origin == SamplingDecision::Origin::DELEGATED);
      return "delegated";
  }
}

}  // namespace tracing
}  // namespace datadog
