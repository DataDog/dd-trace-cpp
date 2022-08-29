#include "sampling_decision.h"

#include <ostream>

namespace datadog {
namespace tracing {

void SamplingDecision::to_json(std::ostream& stream) const {
  stream << "{\"priority\": " << priority << ", \"mechanism\": ";
  if (mechanism) {
    stream << *mechanism;
  } else {
    stream << "null";
  }

  if (configured_rate) {
    stream << ", \"configured_rate\": " << *configured_rate;
  }
  if (limiter_effective_rate) {
    stream << ", \"limiter_effective_rate\": " << *limiter_effective_rate;
  }

  stream << ", \"origin\": \"";
  switch (origin) {
    case Origin::EXTRACTED:
      stream << "EXTRACTED(";
      break;
    case Origin::LOCAL:
      stream << "LOCAL(";
      break;
    case Origin::DELEGATED:
      stream << "DELEGATED(";
      break;
  }
  stream << int(origin) << ")\"}";
}

}  // namespace tracing
}  // namespace datadog
