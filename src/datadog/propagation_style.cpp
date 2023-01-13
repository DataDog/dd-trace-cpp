#include "propagation_style.h"

#include <cassert>

#include "json.hpp"

namespace datadog {
namespace tracing {

nlohmann::json to_json(PropagationStyle style) {
  // Note: Make sure that these strings are consistent (modulo case) with
  // `parse_propagation_styles` in `tracer_config.cpp`.
  switch (style) {
    case PropagationStyle::DATADOG:
      return "Datadog";
    case PropagationStyle::B3:
      return "B3";
    case PropagationStyle::W3C:
      return "tracecontext";  // for compatibility with OpenTelemetry
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
