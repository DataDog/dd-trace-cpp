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
  if (entry && entry->source == StableConfigSource::FLEET) {
    return StringView{entry->value.data(), entry->value.size()};
  }
  if (Optional<StringView> value = lookup(variable)) {
    return value;
  }
  if (entry) {
    return StringView{entry->value.data(), entry->value.size()};
  }
  return nullopt;
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
