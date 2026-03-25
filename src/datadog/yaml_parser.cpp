#include "yaml_parser.h"

#include <yaml-cpp/yaml.h>

#include <sstream>
#include <string>

namespace datadog {
namespace tracing {

YamlParseStatus parse_yaml(const std::string& content, YamlParseResult& out) {
  if (content.empty()) {
    return YamlParseStatus::OK;
  }

  YAML::Node root;
  try {
    root = YAML::Load(content);
  } catch (const std::exception&) {
    return YamlParseStatus::PARSE_ERROR;
  } catch (...) {
    return YamlParseStatus::PARSE_ERROR;
  }

  if (!root.IsDefined() || root.IsNull()) {
    return YamlParseStatus::OK;
  }

  if (!root.IsMap()) {
    return YamlParseStatus::PARSE_ERROR;
  }

  if (root["config_id"]) {
    out.config_id = root["config_id"].as<std::string>();
  }

  if (root["apm_configuration_default"]) {
    const auto& apm = root["apm_configuration_default"];
    if (!apm.IsMap()) {
      return YamlParseStatus::PARSE_ERROR;
    }

    for (const auto& kv : apm) {
      const auto& value_node = kv.second;

      // Skip non-scalar values (sequences, maps, etc.).
      if (!value_node.IsScalar() && !value_node.IsNull()) {
        continue;
      }

      std::string value;
      if (value_node.IsScalar()) {
        // Use the scalar value directly. yaml-cpp preserves the original
        // text representation, so booleans stay as "true"/"false" and
        // numbers stay as their original string form.
        value = value_node.Scalar();
      }

      out.values[kv.first.as<std::string>()] = value;
    }
  }

  return YamlParseStatus::OK;
}

}  // namespace tracing
}  // namespace datadog
