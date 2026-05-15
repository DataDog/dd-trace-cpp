#include <datadog/environment.h>

#include <cstdlib>
#include <string>

#include "json.hpp"

namespace datadog {
namespace tracing {
namespace environment {

StringView name(Variable variable) { return variable_names[variable]; }

Optional<StringView> lookup(Variable variable) {
  const char *name = variable_names[variable];
  const char *value = std::getenv(name);
  if (!value) {
    return nullopt;
  }
  return StringView{value};
}

Optional<StringView> lookup(StringView name) {
  // getenv requires a null-terminated string; copy through std::string.
  std::string null_terminated{name};
  const char *value = std::getenv(null_terminated.c_str());
  if (!value) {
    return nullopt;
  }
  return StringView{value};
}

std::string to_json() {
  auto result = nlohmann::json::object({});

  for (const char *name : variable_names) {
    if (const char *value = std::getenv(name)) {
      result[name] = value;
    }
  }

  return result.dump();
}

}  // namespace environment
}  // namespace tracing
}  // namespace datadog
