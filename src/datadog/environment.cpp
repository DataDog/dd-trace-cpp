#include "environment.h"

#include <cstdlib>

#include "json.hpp"

namespace datadog {
namespace tracing {
namespace environment {

std::string_view name(Variable variable) { return variable_names[variable]; }

std::optional<std::string_view> lookup(Variable variable) {
  const char *name = variable_names[variable];
  const char *value = std::getenv(name);
  if (!value) {
    return std::nullopt;
  }
  return std::string_view{value};
}

void environment_to_json(nlohmann::json &destination) {
  destination = nlohmann::json::object({});
  for (const char *name : variable_names) {
    if (const char *value = std::getenv(name)) {
      destination[name] = value;
    }
  }
}

}  // namespace environment
}  // namespace tracing
}  // namespace datadog
