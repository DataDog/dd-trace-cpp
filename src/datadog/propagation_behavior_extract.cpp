#include <datadog/propagation_behavior_extract.h>

#include <cassert>

#include "json.hpp"
#include "string_util.h"

namespace datadog {
namespace tracing {

StringView to_string_view(PropagationBehaviorExtract behavior) {
  switch (behavior) {
    case PropagationBehaviorExtract::CONTINUE:
      return "continue";
    case PropagationBehaviorExtract::RESTART:
      return "restart";
    case PropagationBehaviorExtract::IGNORE:
      return "ignore";
    default:
      std::abort();
  }
}

nlohmann::json to_json(PropagationBehaviorExtract behavior) {
  return to_string_view(behavior);
}

Optional<PropagationBehaviorExtract> parse_propagation_behavior_extract(
    StringView text) {
  auto token = std::string{text};
  to_lower(token);

  if (token == "continue" || token.empty()) {
    return PropagationBehaviorExtract::CONTINUE;
  } else if (token == "restart") {
    return PropagationBehaviorExtract::RESTART;
  } else if (token == "ignore") {
    return PropagationBehaviorExtract::IGNORE;
  }
  return nullopt;
}

}  // namespace tracing
}  // namespace datadog
