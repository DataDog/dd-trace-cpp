#include "environment.h"

#include <cstdlib>
#ifdef _MSC_VER
#include <winbase.h>  // GetEnvironmentVariable
#include <windows.h>
#endif

#include "json.hpp"

namespace datadog {
namespace tracing {
namespace environment {
namespace {

Optional<std::string> get_env(const char *name) {
#ifdef _MSC_VER
  // maximum size of an environment variable value on Windows
  char buffer[32767];
  const DWORD rc = GetEnvironmentVariable(name, buffer, sizeof buffer);
  if (rc == 0 && GetLastError() == ERROR_ENVVAR_NOT_FOUND) {
    return nullopt;
  }
  return std::string(buffer, rc);  // `rc` is the length on success
#else
  if (const char *value = std::getenv(name)) {
    return value;
  }
  return nullopt;
#endif
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
