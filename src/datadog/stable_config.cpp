#include <datadog/stable_config.h>

namespace datadog::tracing {

void StableConfig::set(std::string name, StableConfigEntry entry) {
  entries_.insert_or_assign(std::move(name), std::move(entry));
}

const StableConfigEntry* StableConfig::find(StringView name) const {
  const auto entry = entries_.find(std::string(name.data(), name.size()));
  return entry == entries_.end() ? nullptr : &entry->second;
}

ConfigValueSource config_value_source(environment::Variable variable,
                                      const StableConfig* stable_config) {
  const StableConfigEntry* entry =
      stable_config ? stable_config->find(environment::name(variable))
                    : nullptr;
  if (entry && (entry->source == StableConfigSource::FLEET ||
                !environment::lookup(variable))) {
    return {entry->source == StableConfigSource::FLEET
                ? ConfigMetadata::Origin::FLEET_STABLE_CONFIG
                : ConfigMetadata::Origin::LOCAL_STABLE_CONFIG,
            entry->config_id.empty() ? nullopt
                                     : Optional<std::string>{entry->config_id}};
  }
  return {ConfigMetadata::Origin::ENVIRONMENT_VARIABLE, nullopt};
}

int config_value_priority(environment::Variable variable,
                          const StableConfig* stable_config) {
  if (!environment::lookup(variable, stable_config)) return -1;
  if (!stable_config) return 0;

  switch (config_value_source(variable, stable_config).origin) {
    case ConfigMetadata::Origin::LOCAL_STABLE_CONFIG:
      return 0;
    case ConfigMetadata::Origin::ENVIRONMENT_VARIABLE:
      return 1;
    case ConfigMetadata::Origin::FLEET_STABLE_CONFIG:
      return 2;
    default:
      return -1;
  }
}

}  // namespace datadog::tracing
