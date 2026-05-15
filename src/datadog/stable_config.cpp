#include "stable_config.h"

#include <sys/stat.h>

#include <cerrno>
#include <cstddef>
#include <cstring>
#include <fstream>
#include <ostream>
#include <string>

#include "yaml_parser.h"

namespace datadog {
namespace tracing {
namespace {

// Maximum file size accepted for stable configuration files: 256KB.
// This is a file I/O concern, not a parser concern, so it lives here rather
// than in yaml_parser.h.
constexpr std::size_t kMaxYamlFileSize = 256 * 1024;

}  // namespace

// Read a file and parse it into a StableConfig. Logs warnings on errors.
// Returns an empty StableConfig if the file doesn't exist or can't be read.
StableConfig load_one(const std::string& path, Logger& logger) {
  StableConfig result;

  // Probe for existence before opening so we can distinguish a missing file
  // (silent no-op) from a present-but-unreadable file (permission / I/O
  // error worth logging).
  struct stat st;
  if (::stat(path.c_str(), &st) != 0) {
    // Most often ENOENT — file simply isn't there. Silent skip.
    return result;
  }

  std::ifstream file(path, std::ios::binary | std::ios::ate);
  if (!file.is_open()) {
    const int err = errno;
    logger.log_error([&path, err](std::ostream& log) {
      log << "Stable config: file " << path << " exists but could not be "
          << "opened (errno " << err << ": " << std::strerror(err)
          << "); skipping.";
    });
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
  if (!file.read(content.data(), size)) {
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

StableConfigPaths get_stable_config_paths() {
  return {
      "/etc/datadog-agent/application_monitoring.yaml",
      "/etc/datadog-agent/managed/datadog-agent/stable/"
      "application_monitoring.yaml",
  };
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
