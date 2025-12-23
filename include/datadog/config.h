#pragma once

#include <string>
#include <type_traits>
#include <unordered_map>
#include <utility>
#include <vector>

#include "error.h"
#include "optional.h"

namespace datadog {
namespace tracing {

// Enumerates available configuration names for the tracing library
enum class ConfigName : char {
  SERVICE_NAME,
  SERVICE_ENV,
  SERVICE_VERSION,
  REPORT_TRACES,
  TAGS,
  EXTRACTION_STYLES,
  INJECTION_STYLES,
  STARTUP_LOGS,
  REPORT_TELEMETRY,
  DELEGATE_SAMPLING,
  GENEREATE_128BIT_TRACE_IDS,
  AGENT_URL,
  RC_POLL_INTERVAL,
  TRACE_SAMPLING_RATE,
  TRACE_SAMPLING_LIMIT,
  TRACE_SAMPLING_RULES,
  SPAN_SAMPLING_RULES,
  TRACE_BAGGAGE_MAX_BYTES,
  TRACE_BAGGAGE_MAX_ITEMS,
  APM_TRACING_ENABLED,
  TRACE_RESOURCE_RENAMING_ENABLED,
  TRACE_RESOURCE_RENAMING_ALWAYS_SIMPLIFIED_ENDPOINT,
};

// Represents metadata for configuration parameters
struct ConfigMetadata {
  enum class Origin : char {
    ENVIRONMENT_VARIABLE,  // Originating from environment variables
    CODE,                  // Defined in code
    REMOTE_CONFIG,         // Retrieved from remote configuration
    DEFAULT                // Default value
  };

  // Name of the configuration parameter
  ConfigName name;
  // Value of the configuration parameter
  std::string value;
  // Origin of the configuration parameter
  Origin origin;
  // Optional error associated with the configuration parameter
  Optional<Error> error;

  ConfigMetadata() = default;
  ConfigMetadata(ConfigName n, std::string v, Origin orig,
                 Optional<Error> err = nullopt)
      : name(n), value(std::move(v)), origin(orig), error(std::move(err)) {}
};

// Return the chosen configuration value, in order of precedence: `from_env`,
// `from_user`, and `fallback`.
// This overload directly populates both telemetry_configs[config_name] and
// metadata[config_name] using the stringified value, with all values found,
// from lowest to highest precedence. Returns the chosen value directly.
//
// The fallback parameter is optional and defaults to nullptr.
template <typename Value, typename Stringifier = std::nullptr_t,
          typename DefaultValue = std::nullptr_t>
Value pick(const Optional<Value>& from_env, const Optional<Value>& from_user,
           std::unordered_map<ConfigName, std::vector<ConfigMetadata>>*
               telemetry_configs,
           std::unordered_map<ConfigName, ConfigMetadata>* metadata,
           ConfigName config_name, DefaultValue fallback = nullptr,
           Stringifier to_string_fn = nullptr) {
  auto stringify = [&](const Value& v) -> std::string {
    if constexpr (!std::is_same_v<Stringifier, std::nullptr_t>) {
      return to_string_fn(v);  // use provided function
    } else if constexpr (std::is_constructible_v<std::string, Value>) {
      return std::string(v);  // default behaviour (works for string-like types)
    } else {
      static_assert(!std::is_same_v<Value, Value>,
                    "Non-string types require a stringifier function");
      return "";  // unreachable
    }
  };

  std::vector<ConfigMetadata> telemetry_entries;
  Optional<Value> chosen_value;

  auto add_entry = [&](ConfigMetadata::Origin origin, const Value& val) {
    std::string val_str = stringify(val);
    telemetry_entries.emplace_back(
        ConfigMetadata{config_name, val_str, origin});
    chosen_value = val;
  };

  // Add DEFAULT entry if fallback was provided (detected by type)
  if constexpr (!std::is_same_v<DefaultValue, std::nullptr_t>) {
    add_entry(ConfigMetadata::Origin::DEFAULT, fallback);
  }

  if (from_user) {
    add_entry(ConfigMetadata::Origin::CODE, *from_user);
  }

  if (from_env) {
    add_entry(ConfigMetadata::Origin::ENVIRONMENT_VARIABLE, *from_env);
  }

  (*telemetry_configs)[config_name] = std::move(telemetry_entries);
  if (!(*telemetry_configs)[config_name].empty()) {
    (*metadata)[config_name] = (*telemetry_configs)[config_name].back();
  }

  return chosen_value.value_or(Value{});
}

// Return a pair containing the configuration origin and value of a
// configuration value chosen from one of the specified `from_env`,
// `from_config`, and `fallback`. This function defines the relative precedence
// among configuration values originating from the environment, programmatic
// configuration, and default configuration.
template <typename Value, typename DefaultValue>
std::pair<ConfigMetadata::Origin, Value> pick(const Optional<Value>& from_env,
                                              const Optional<Value>& from_user,
                                              DefaultValue fallback) {
  if (from_env) {
    return {ConfigMetadata::Origin::ENVIRONMENT_VARIABLE, *from_env};
  } else if (from_user) {
    return {ConfigMetadata::Origin::CODE, *from_user};
  }
  return {ConfigMetadata::Origin::DEFAULT, fallback};
}

}  // namespace tracing
}  // namespace datadog
