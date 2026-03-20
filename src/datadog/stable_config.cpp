#include "stable_config.h"

#include <fstream>
#include <string>

#include "yaml_parser.h"

#ifdef _WIN32
#include <windows.h>
// windows.h defines ERROR as a macro which conflicts with our enum.
#undef ERROR
#endif

namespace datadog {
namespace tracing {
namespace {

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

  if (static_cast<std::size_t>(size) > kMaxYamlFileSize) {
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

  YamlParseResult parsed;
  if (parse_yaml(content, parsed) != YamlParseStatus::OK) {
    logger.log_error([&path](std::ostream& log) {
      log << "Stable config: malformed YAML in " << path << "; skipping.";
    });
    return {};  // Return empty config on parse error.
  }

  result.config_id = std::move(parsed.config_id);
  result.values = std::move(parsed.values);
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
