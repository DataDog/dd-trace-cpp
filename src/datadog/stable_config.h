#pragma once

// This component provides support for reading "stable configuration" from
// YAML files on disk. Two files are read at tracer initialization:
//
//   - A "local" (user-managed) file
//   - A "fleet" (fleet-managed) file
//
// Each file may contain a flat map of DD_* environment variable names to
// scalar values under the `apm_configuration_default` key. These values
// participate in configuration precedence:
//
//   fleet_stable > env > user/code > local_stable > default

#include <datadog/logger.h>
#include <datadog/optional.h>

#include <string>
#include <unordered_map>

namespace datadog {
namespace tracing {

// Paths to the two stable configuration files.
struct StableConfigPaths {
  std::string local_path;
  std::string fleet_path;
};

// Return the platform-specific paths for stable configuration files.
StableConfigPaths get_stable_config_paths();

// Parsed contents of one stable configuration file.
struct StableConfig {
  // Config ID from the file (optional, for telemetry).
  Optional<std::string> config_id;

  // Map of environment variable names (e.g. "DD_SERVICE") to string values.
  std::unordered_map<std::string, std::string> values;

  // Look up a config key, returning nullopt if not present.
  Optional<std::string> lookup(const std::string& key) const;
};

// Holds both the local and fleet stable configs.
struct StableConfigs {
  StableConfig local;
  StableConfig fleet;
};

// Load and parse both stable configuration files.
// Returns empty configs (no error) if files don't exist.
StableConfigs load_stable_configs(Logger& logger);

}  // namespace tracing
}  // namespace datadog
