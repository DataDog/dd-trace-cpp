#include "yaml_parser.h"

#include <yaml-cpp/yaml.h>

#include <cctype>
#include <string>

namespace datadog {
namespace tracing {

YamlParseStatus parse_yaml(const std::string& content, YamlParseResult& out) {
  if (content.empty()) {
    return YamlParseStatus::OK;
  }

  // Reject documents with an excessive number of alias references, as a
  // best-effort defense against alias-expansion bombs. yaml-cpp 0.8.0 has
  // no built-in limit on alias expansion. 100 is well above legitimate use
  // (the configs are flat string maps) and well below what an expansion
  // attack would require to matter.
  constexpr std::size_t kMaxAliases = 100;
  std::size_t alias_count = 0;
  for (std::size_t i = 0; i + 1 < content.size(); ++i) {
    // A YAML alias is '*' followed by an identifier character. The simple
    // heuristic of counting '*' followed by an alnum / underscore is good
    // enough — this is a guard, not a parser.
    if (content[i] == '*' &&
        (std::isalnum(static_cast<unsigned char>(content[i + 1])) ||
         content[i + 1] == '_')) {
      if (++alias_count > kMaxAliases) {
        return YamlParseStatus::PARSE_ERROR;
      }
    }
  }

  YAML::Node root;
  try {
    root = YAML::Load(content);
  } catch (...) {
    return YamlParseStatus::PARSE_ERROR;
  }

  if (!root.IsDefined() || root.IsNull()) {
    return YamlParseStatus::OK;
  }

  if (!root.IsMap()) {
    return YamlParseStatus::PARSE_ERROR;
  }

  if (const auto& config_id_node = root["config_id"]) {
    if (!config_id_node.IsScalar()) {
      return YamlParseStatus::PARSE_ERROR;
    }
    try {
      out.config_id = config_id_node.as<std::string>();
    } catch (...) {
      return YamlParseStatus::PARSE_ERROR;
    }
  }

  if (const auto& apm = root["apm_configuration_default"]) {
    if (!apm.IsMap()) {
      return YamlParseStatus::PARSE_ERROR;
    }

    for (const auto& kv : apm) {
      if (!kv.first.IsScalar()) {
        return YamlParseStatus::PARSE_ERROR;
      }

      std::string key;
      try {
        key = kv.first.as<std::string>();
      } catch (...) {
        return YamlParseStatus::PARSE_ERROR;
      }

      const auto& value_node = kv.second;
      if (value_node.IsScalar()) {
        // yaml-cpp preserves the original text representation, so booleans
        // stay as "true"/"false" and numbers stay as their original string
        // form.
        out.values[key] = value_node.Scalar();
      } else if (value_node.IsNull()) {
        // Null value (e.g., `DD_SERVICE:`) is stored as the empty string.
        out.values[key] = "";
      }
      // Sequences, maps, etc. are silently skipped.
    }
  }

  return YamlParseStatus::OK;
}

}  // namespace tracing
}  // namespace datadog
