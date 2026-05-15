#include "environment_source.h"

#include <datadog/environment.h>

#include <string>

namespace datadog {
namespace tracing {

Optional<std::string> EnvironmentSource::lookup(StringView key) const {
  auto value = environment::lookup(key);
  if (!value) return nullopt;
  // An explicit empty value is preserved (matches environment::lookup
  // semantics).  Callers that want to ignore empty values should do so
  // after lookup.
  return std::string{*value};
}

ConfigMetadata::Origin EnvironmentSource::origin() const {
  return ConfigMetadata::Origin::ENVIRONMENT_VARIABLE;
}

}  // namespace tracing
}  // namespace datadog
