#include <datadog/environment.h>
#include <datadog/stable_config.h>

#include <cstdlib>

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

Optional<StringView> lookup(Variable variable,
                            const StableConfig *stable_config) {
  const StableConfigEntry *entry =
      stable_config ? stable_config->find(name(variable)) : nullptr;
  // Fleet config wins over the environment. Local config loses to it.
  const Optional<StringView> value = lookup(variable);
  if (entry && (entry->source == StableConfigSource::FLEET || !value)) {
    return StringView{entry->value};
  }
  return value;
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
