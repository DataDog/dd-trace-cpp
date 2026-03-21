#pragma once

// This component provides a minimal YAML parser for stable configuration files.
// It extracts a config_id and a flat key-value map from the
// `apm_configuration_default` section of a YAML document.

#include <datadog/optional.h>

#include <string>
#include <unordered_map>

namespace datadog {
namespace tracing {

// Result of parsing a YAML stable configuration document.
struct YamlParseResult {
  // Config ID from the file (optional).
  Optional<std::string> config_id;

  // Map of environment variable names (e.g. "DD_SERVICE") to string values,
  // extracted from the `apm_configuration_default` section.
  std::unordered_map<std::string, std::string> values;
};

enum class YamlParseStatus { OK, PARSE_ERROR };

// Parse the given YAML content string into a YamlParseResult.
// Returns OK on success (including when `apm_configuration_default` is absent).
// Returns PARSE_ERROR on malformed input.
YamlParseStatus parse_yaml(const std::string& content, YamlParseResult& out);

}  // namespace tracing
}  // namespace datadog
