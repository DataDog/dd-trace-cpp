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
  if (val == nullptr) return nullopt;
  // An explicit empty value is preserved (matches environment::lookup
  // semantics): callers that want to ignore empty values should do so
  // after lookup.
  return std::string{val};
}

ConfigMetadata::Origin EnvironmentSource::origin() const {
  return ConfigMetadata::Origin::ENVIRONMENT_VARIABLE;
}

}  // namespace tracing
}  // namespace datadog
