#include "sampling_decision.h"

#include <iomanip>
#include <ostream>

namespace datadog {
namespace tracing {

void SamplingDecision::to_json(std::ostream& stream) const {
  stream << "{\"keep\": " << std::boolalpha << keep << ", \"mechanism\": ";
  if (mechanism) {
    stream << int(*mechanism);
  } else {
    stream << "null";
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
  stream << int(origin)
         << ")\", \"awaiting_delegated_decision\": " << std::boolalpha
         << awaiting_delegated_decision << "}";
}

}  // namespace tracing
}  // namespace datadog
