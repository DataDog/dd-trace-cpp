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
    DEFAULT,               // Default value
    LOCAL_STABLE_CONFIG,   // From local stable config file
    FLEET_STABLE_CONFIG    // From fleet stable config file
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

// 3-parameter overload (env, user, default) kept for backward compatibility
// with external projects (e.g., nginx-datadog, httpd-datadog) that include
// this public header.  New internal code should prefer the 5-parameter
// overload that also accepts fleet and local stable config sources.
//
// Returns the final configuration value using the following
// precedence order: environment > user code > default, and populates metadata:
// `metadata`: Records ALL configuration sources that were provided,
//    ordered from lowest to highest precedence. The last entry has the highest
//    precedence and is the winning value.
//
// Template Parameters:
//   Value: The type of the configuration value
//   Stringifier: Optional function type to convert Value to string
//                (defaults to std::nullptr_t, which uses string construction)
//   DefaultValue: Type of the fallback value (defaults to std::nullptr_t)
//
// Parameters:
//   from_env: Optional value from environment variables (highest precedence)
//   from_user: Optional value from user code (middle precedence)
//   metadata: Output map that will be populated with all config sources found
//             for this config_name, in precedence order (last = highest)
//   config_name: The configuration parameter name identifier
//   fallback: Optional default value (lowest precedence). Pass nullptr to
//             indicate no default.
//   to_string_fn: Optional custom function to convert Value to string.
//                 Required for non-string types. For string-like types, uses
//                 default string construction if not provided.
//
// Returns:
//   The chosen configuration value based on precedence, or Value{} if no value
//   was provided.
template <typename Value, typename Stringifier = std::nullptr_t,
          typename DefaultValue = std::nullptr_t>
Value resolve_and_record_config(
    const Optional<Value>& from_env, const Optional<Value>& from_user,
    std::unordered_map<ConfigName, std::vector<ConfigMetadata>>* metadata,
    ConfigName config_name, DefaultValue fallback = nullptr,
    Stringifier to_string_fn = nullptr) {
  // Delegate to the 5-parameter overload with nullopt for fleet and local
  // stable config sources.
  return resolve_and_record_config(Optional<Value>{}, from_env, from_user,
                                   Optional<Value>{}, metadata, config_name,
                                   fallback, to_string_fn);
}

// Extended version of resolve_and_record_config that includes stable
// configuration sources.  Precedence order (highest to lowest):
//   fleet_stable > env > user/code > local_stable > default
template <typename Value, typename Stringifier = std::nullptr_t,
          typename DefaultValue = std::nullptr_t>
Value resolve_and_record_config(
    const Optional<Value>& from_fleet_stable, const Optional<Value>& from_env,
    const Optional<Value>& from_user, const Optional<Value>& from_local_stable,
    std::unordered_map<ConfigName, std::vector<ConfigMetadata>>* metadata,
    ConfigName config_name, DefaultValue fallback = nullptr,
    Stringifier to_string_fn = nullptr) {
  auto stringify = [&](const Value& v) -> std::string {
    if constexpr (!std::is_same_v<Stringifier, std::nullptr_t>) {
      return to_string_fn(v);
    } else if constexpr (std::is_constructible_v<std::string, Value>) {
      return std::string(v);
    } else {
      static_assert(!std::is_same_v<Value, Value>,
                    "Non-string types require a stringifier function");
      return "";
    }
  };

  std::vector<ConfigMetadata> metadata_entries;
  Optional<Value> chosen_value;

  auto add_entry = [&](ConfigMetadata::Origin origin, const Value& val) {
    std::string val_str = stringify(val);
    metadata_entries.emplace_back(ConfigMetadata{config_name, val_str, origin});
    chosen_value = val;
  };

  // Precedence: default < local_stable < user/code < env < fleet_stable
  if constexpr (!std::is_same_v<DefaultValue, std::nullptr_t>) {
    add_entry(ConfigMetadata::Origin::DEFAULT, fallback);
  }

  if (from_local_stable) {
    add_entry(ConfigMetadata::Origin::LOCAL_STABLE_CONFIG, *from_local_stable);
  }

  if (from_user) {
    add_entry(ConfigMetadata::Origin::CODE, *from_user);
  }

  if (from_env) {
    add_entry(ConfigMetadata::Origin::ENVIRONMENT_VARIABLE, *from_env);
  }

  if (from_fleet_stable) {
    add_entry(ConfigMetadata::Origin::FLEET_STABLE_CONFIG, *from_fleet_stable);
  }

  if (!metadata_entries.empty()) {
    (*metadata)[config_name] = std::move(metadata_entries);
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
