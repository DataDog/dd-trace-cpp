#include "environment.h"

#include <cstdlib>

#include "json.hpp"

namespace datadog {
namespace tracing {
namespace environment {
namespace {

Optional<std::string> get_env(const char *name) {
  if (const char *value = std::getenv(name)) {
    return value;
  }
  return nullopt;
}

}  // namespace

StringView name(Variable variable) { return variable_names[variable]; }

Optional<std::string> lookup(Variable variable) {
  const char *name = variable_names[variable];
  return get_env(name);
}

nlohmann::json to_json() {
  auto result = nlohmann::json::object({});

  for (const char *name : variable_names) {
    if (auto value = get_env(name)) {
      result[name] = std::move(*value);
    }
  }

  return result;
}

}  // namespace environment
}  // namespace tracing
}  // namespace datadog
