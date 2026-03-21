#include "yaml_parser.h"

#include <sstream>
#include <string>

namespace datadog {
namespace tracing {
namespace {

// Remove leading and trailing whitespace from `s`.
// This is intentionally a local function rather than reusing string_util.h's
// trim(StringView), because this version also strips '\r' and '\n' to handle
// Windows line endings in YAML files.
std::string trim(const std::string& s) {
  const auto begin = s.find_first_not_of(" \t\r\n");
  if (begin == std::string::npos) return "";
  const auto end = s.find_last_not_of(" \t\r\n");
  return s.substr(begin, end - begin + 1);
}

// If `s` is surrounded by matching quotes (single or double), remove them and
// return the inner content. Otherwise return `s` as-is.
std::string unquote(const std::string& s) {
  if (s.size() >= 2) {
    const char front = s.front();
    const char back = s.back();
    if ((front == '"' && back == '"') || (front == '\'' && back == '\'')) {
      return s.substr(1, s.size() - 2);
    }
  }
  return s;
}

// Strip an inline comment from a value string. Handles quoted values so that
// a '#' inside quotes is not treated as a comment.
std::string strip_inline_comment(const std::string& s) {
  if (s.empty()) return s;

  // If the value starts with a quote, find the closing quote first.
  if (s[0] == '"' || s[0] == '\'') {
    const char quote = s[0];
    auto close = s.find(quote, 1);
    if (close != std::string::npos) {
      // Return just the quoted value (anything after closing quote + whitespace
      // + '#' is comment).
      return s.substr(0, close + 1);
    }
    // No closing quote — return as-is (will be treated as a parse issue
    // elsewhere or kept verbatim).
    return s;
  }

  // Unquoted value: '#' starts a comment.
  auto pos = s.find('#');
  if (pos != std::string::npos) {
    auto result = s.substr(0, pos);
    // Trim trailing whitespace before the comment.
    auto end = result.find_last_not_of(" \t");
    if (end != std::string::npos) {
      return result.substr(0, end + 1);
    }
    return "";
  }
  return s;
}

}  // namespace

YamlParseStatus parse_yaml(const std::string& content, YamlParseResult& out) {
  std::istringstream stream(content);
  std::string line;
  bool in_apm_config = false;

  while (std::getline(stream, line)) {
    // Remove carriage return if present (Windows line endings).
    if (!line.empty() && line.back() == '\r') {
      line.pop_back();
    }

    // Strip comments from lines that are entirely comments.
    auto trimmed = trim(line);
    if (trimmed.empty() || trimmed[0] == '#') {
      continue;
    }

    // Skip YAML document markers (start '---' and end '...').
    if (trimmed == "---" || trimmed == "...") {
      continue;
    }

    // Detect indentation to know if we're in a map or at the top level.
    const auto first_non_space = line.find_first_not_of(" \t");
    const bool is_indented = (first_non_space > 0);

    if (!is_indented) {
      // Top-level key.
      in_apm_config = false;

      auto colon_pos = trimmed.find(':');
      if (colon_pos == std::string::npos) {
        // Malformed line at top level.
        return YamlParseStatus::PARSE_ERROR;
      }

      auto key = trim(trimmed.substr(0, colon_pos));
      auto value = trim(trimmed.substr(colon_pos + 1));

      // Strip inline comment from value.
      value = strip_inline_comment(value);
      value = trim(value);

      if (key == "apm_configuration_default") {
        in_apm_config = true;
        // The value after the colon should be empty (map follows on next
        // lines). If it's not empty, that's malformed for our purposes.
        if (!value.empty()) {
          return YamlParseStatus::PARSE_ERROR;
        }
      } else if (key == "config_id") {
        out.config_id = unquote(value);
      }
      // Unknown top-level keys are silently ignored.
    } else if (in_apm_config) {
      // Indented line under apm_configuration_default.
      auto colon_pos = trimmed.find(':');
      if (colon_pos == std::string::npos) {
        // Malformed entry.
        return YamlParseStatus::PARSE_ERROR;
      }

      auto key = trim(trimmed.substr(0, colon_pos));
      auto value = trim(trimmed.substr(colon_pos + 1));

      // Strip inline comment.
      value = strip_inline_comment(value);
      value = trim(value);

      // Check for non-scalar values (flow sequences/mappings), but only
      // for unquoted values.  A quoted value like '[{"rate":1}]' is a scalar
      // string that happens to contain JSON.
      const bool is_quoted = value.size() >= 2 &&
                             ((value.front() == '"' && value.back() == '"') ||
                              (value.front() == '\'' && value.back() == '\''));
      if (!is_quoted && !value.empty() &&
          (value[0] == '[' || value[0] == '{' || value[0] == '|' ||
           value[0] == '>')) {
        continue;
      }

      // Unquote the value.
      value = unquote(value);

      // Store the key-value pair. Last value wins for duplicates.
      out.values[key] = value;
    }
    // Indented lines under unknown top-level keys are silently ignored.
  }

  return YamlParseStatus::OK;
}

}  // namespace tracing
}  // namespace datadog
