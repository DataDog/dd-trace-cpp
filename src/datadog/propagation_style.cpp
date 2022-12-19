#include "propagation_style.h"

#include <cassert>

#include "json.hpp"

namespace datadog {
namespace tracing {

nlohmann::json to_json(PropagationStyle style) {
  switch (style) {
    case PropagationStyle::DATADOG:
      return "datadog";
    case PropagationStyle::B3:
      return "B3";
    default:
      assert(style == PropagationStyle::NONE);
      return "none";
  }
}

nlohmann::json to_json(const std::vector<PropagationStyle>& styles) {
  std::vector<nlohmann::json> styles_json;
  for (const auto style : styles) {
    styles_json.push_back(to_json(style));
  }
  return styles_json;
}

}  // namespace tracing
}  // namespace datadog
