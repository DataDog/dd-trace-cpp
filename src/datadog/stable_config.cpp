#include "stable_config.h"

#include <algorithm>
#include <fstream>
#include <sstream>
#include <string>

#ifdef _WIN32
#include <windows.h>
#endif

namespace datadog {
namespace tracing {
namespace {

// Maximum file size: 256KB.
constexpr std::size_t kMaxFileSize = 256 * 1024;

#ifdef _WIN32

std::string get_windows_agent_dir() {
  // Try to read the agent directory from the Windows registry.
  // Keys: HKLM\SOFTWARE\Datadog\Datadog Agent -> ConfigRoot or InstallPath
  HKEY key;
  if (RegOpenKeyExA(HKEY_LOCAL_MACHINE, "SOFTWARE\\Datadog\\Datadog Agent", 0,
                    KEY_READ, &key) == ERROR_SUCCESS) {
    char buffer[MAX_PATH];
    DWORD size = sizeof(buffer);
    DWORD type = 0;

    // Try ConfigRoot first.
    if (RegQueryValueExA(key, "ConfigRoot", nullptr, &type,
                         reinterpret_cast<LPBYTE>(buffer),
                         &size) == ERROR_SUCCESS &&
        type == REG_SZ && size > 0) {
      RegCloseKey(key);
      std::string result(buffer, size - 1);  // exclude null terminator
      // Ensure trailing backslash.
      if (!result.empty() && result.back() != '\\') {
        result += '\\';
      }
      return result;
    }

    // Try InstallPath.
    size = sizeof(buffer);
    if (RegQueryValueExA(key, "InstallPath", nullptr, &type,
                         reinterpret_cast<LPBYTE>(buffer),
                         &size) == ERROR_SUCCESS &&
        type == REG_SZ && size > 0) {
      RegCloseKey(key);
      std::string result(buffer, size - 1);
      if (!result.empty() && result.back() != '\\') {
        result += '\\';
      }
      return result;
    }

    RegCloseKey(key);
  }

  // Default path.
  return "C:\\ProgramData\\Datadog\\";
}

#endif  // _WIN32

// Remove leading and trailing whitespace from `s`.
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

enum class ParseResult { OK, ERROR };

// Parse a YAML file's contents into a StableConfig.
// Returns OK on success (including empty/missing apm_configuration_default).
// Returns ERROR on malformed input.
ParseResult parse_yaml(const std::string& content, StableConfig& out) {
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

    // Detect indentation to know if we're in a map or at the top level.
    const auto first_non_space = line.find_first_not_of(" \t");
    const bool is_indented = (first_non_space > 0);

    if (!is_indented) {
      // Top-level key.
      in_apm_config = false;

      auto colon_pos = trimmed.find(':');
      if (colon_pos == std::string::npos) {
        // Malformed line at top level.
        return ParseResult::ERROR;
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
          return ParseResult::ERROR;
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
        return ParseResult::ERROR;
      }

      auto key = trim(trimmed.substr(0, colon_pos));
      auto value = trim(trimmed.substr(colon_pos + 1));

      // Strip inline comment.
      value = strip_inline_comment(value);
      value = trim(value);

      // Check for non-scalar values (flow sequences/mappings).
      if (!value.empty() && (value[0] == '[' || value[0] == '{' ||
                             value[0] == '|' || value[0] == '>')) {
        // Skip non-scalar values silently (as per spec: "log warning, skip
        // that entry").
        continue;
      }

      // Unquote the value.
      value = unquote(value);

      // Store the key-value pair. Last value wins for duplicates.
      out.values[key] = value;
    }
    // Indented lines under unknown top-level keys are silently ignored.
  }

  return ParseResult::OK;
}

// Read a file and parse it into a StableConfig. Logs warnings on errors.
// Returns an empty StableConfig if the file doesn't exist or can't be read.
StableConfig load_one(const std::string& path, Logger& logger) {
  StableConfig result;

  std::ifstream file(path, std::ios::binary | std::ios::ate);
  if (!file.is_open()) {
    // File not found or unreadable — silently skip.
    return result;
  }

  // Check file size.
  const auto size = file.tellg();
  if (size < 0) {
    logger.log_error([&path](std::ostream& log) {
      log << "Stable config: unable to determine size of " << path
          << "; skipping.";
    });
    return result;
  }

  if (static_cast<std::size_t>(size) > kMaxFileSize) {
    logger.log_error([&path](std::ostream& log) {
      log << "Stable config: file " << path
          << " exceeds 256KB size limit; skipping.";
    });
    return result;
  }

  file.seekg(0);
  std::string content(static_cast<std::size_t>(size), '\0');
  if (!file.read(&content[0], size)) {
    logger.log_error([&path](std::ostream& log) {
      log << "Stable config: unable to read " << path << "; skipping.";
    });
    return result;
  }

  if (parse_yaml(content, result) != ParseResult::OK) {
    logger.log_error([&path](std::ostream& log) {
      log << "Stable config: malformed YAML in " << path << "; skipping.";
    });
    return {};  // Return empty config on parse error.
  }

  return result;
}

}  // namespace

StableConfigPaths get_stable_config_paths() {
#ifdef _WIN32
  const auto agent_dir = get_windows_agent_dir();
  return {
      agent_dir + "application_monitoring.yaml",
      agent_dir + "managed\\datadog-agent\\stable\\application_monitoring.yaml",
  };
#else
  return {
      "/etc/datadog-agent/application_monitoring.yaml",
      "/etc/datadog-agent/managed/datadog-agent/stable/"
      "application_monitoring.yaml",
  };
#endif
}

Optional<std::string> StableConfig::lookup(const std::string& key) const {
  auto it = values.find(key);
  if (it != values.end()) {
    return it->second;
  }
  return nullopt;
}

StableConfigs load_stable_configs(Logger& logger) {
  const auto paths = get_stable_config_paths();
  StableConfigs configs;
  configs.local = load_one(paths.local_path, logger);
  configs.fleet = load_one(paths.fleet_path, logger);
  return configs;
}

}  // namespace tracing
}  // namespace datadog
