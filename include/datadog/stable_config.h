#pragma once

#include <string>
#include <unordered_map>

#include "config.h"
#include "environment.h"
#include "string_view.h"

namespace datadog::tracing {

enum class StableConfigSource { LOCAL, FLEET };

struct StableConfigEntry {
  std::string value;
  StableConfigSource source;
  std::string config_id;
};

struct ConfigValueSource {
  ConfigMetadata::Origin origin;
  Optional<std::string> config_id;
};

// Values returned by the libdatadog stable configuration parser.
class StableConfig {
 public:
  void set(std::string name, StableConfigEntry entry);
  const StableConfigEntry* find(StringView name) const;

 private:
  std::unordered_map<std::string, StableConfigEntry> entries_;
};

ConfigValueSource config_value_source(environment::Variable variable,
                                      const StableConfig* stable_config);
int config_value_priority(environment::Variable variable,
                          const StableConfig* stable_config);

template <typename Value, typename DefaultValue = std::nullptr_t,
          typename Stringifier = std::nullptr_t>
Value resolve_with_stable_config(
    const Optional<Value>& from_environment, const Optional<Value>& from_user,
    std::unordered_map<ConfigName, std::vector<ConfigMetadata>>* metadata,
    ConfigName config_name, environment::Variable variable,
    const StableConfig* stable_config, DefaultValue fallback = nullptr,
    Stringifier to_string_fn = nullptr) {
  const ConfigValueSource source = config_value_source(variable, stable_config);
  return resolve_and_record_config(
      from_environment, from_user, metadata, config_name, fallback,
      to_string_fn, source.origin, source.config_id, stable_config != nullptr);
}

template <typename Value, typename Fallback>
Value choose_with_stable_config(const Optional<Value>& from_environment,
                                const Optional<Value>& from_user,
                                Fallback fallback,
                                const StableConfig* stable_config) {
  if (stable_config) {
    return value_or(from_user, from_environment, fallback);
  }
  return value_or(from_environment, from_user, fallback);
}

}  // namespace datadog::tracing
