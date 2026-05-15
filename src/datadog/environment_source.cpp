#include "environment_source.h"

#include <cstdlib>
#include <string>

namespace datadog {
namespace tracing {

Optional<std::string> EnvironmentSource::lookup(StringView key) const {
  // `getenv` requires a null-terminated string.  StringView may not be
  // null-terminated, so copy into std::string.
  std::string null_terminated{key};
  const char* val = std::getenv(null_terminated.c_str());
  if (val == nullptr || val[0] == '\0') return nullopt;
  return std::string{val};
}

ConfigMetadata::Origin EnvironmentSource::origin() const {
  return ConfigMetadata::Origin::ENVIRONMENT_VARIABLE;
}

}  // namespace tracing
}  // namespace datadog
