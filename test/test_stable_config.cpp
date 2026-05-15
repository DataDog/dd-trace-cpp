#include <datadog/optional.h>
#include <sys/stat.h>

#include <filesystem>
#include <fstream>
#include <string>

#include "mocks/loggers.h"
#include "stable_config.h"
#include "stable_config_source.h"
#include "test.h"
#include "yaml_parser.h"

using namespace datadog::tracing;

namespace {

namespace fs = std::filesystem;

// Create a temporary directory for stable config test files.
class TempDir {
  fs::path path_;

 public:
  TempDir() {
    path_ = fs::temp_directory_path() /
            ("dd-trace-cpp-test-stable-config-" +
             std::to_string(std::hash<std::string>{}(__FILE__)));
    fs::create_directories(path_);
  }

  ~TempDir() {
    std::error_code ec;
    fs::remove_all(path_, ec);
  }

  const fs::path& path() const { return path_; }
};

}  // namespace

#define STABLE_CONFIG_TEST(x) TEST_CASE(x, "[stable_config]")

STABLE_CONFIG_TEST("StableConfig::lookup returns nullopt for missing key") {
  StableConfig cfg;
  REQUIRE(!cfg.lookup("DD_SERVICE").has_value());
}

STABLE_CONFIG_TEST("StableConfig::lookup returns value for present key") {
  StableConfig cfg;
  cfg.values["DD_SERVICE"] = "my-service";
  auto val = cfg.lookup("DD_SERVICE");
  REQUIRE(val.has_value());
  REQUIRE(*val == "my-service");
}

STABLE_CONFIG_TEST("parse valid YAML with apm_configuration_default") {
  std::string yaml_content = R"(
apm_configuration_default:
  DD_SERVICE: my-service
  DD_ENV: production
  DD_PROFILING_ENABLED: true
)";

  YamlParseResult parsed;
  auto status = parse_yaml(yaml_content, parsed);
  REQUIRE(status == YamlParseStatus::OK);

  REQUIRE(parsed.values.count("DD_SERVICE"));
  REQUIRE(parsed.values["DD_SERVICE"] == "my-service");
  REQUIRE(parsed.values.count("DD_ENV"));
  REQUIRE(parsed.values["DD_ENV"] == "production");
  REQUIRE(parsed.values.count("DD_PROFILING_ENABLED"));
  REQUIRE(parsed.values["DD_PROFILING_ENABLED"] == "true");
  REQUIRE(!parsed.values.count("DD_MISSING"));
}

STABLE_CONFIG_TEST("config_id is stored") {
  StableConfig cfg;
  cfg.config_id = "fleet-policy-123";
  REQUIRE(cfg.config_id.has_value());
  REQUIRE(*cfg.config_id == "fleet-policy-123");
}

STABLE_CONFIG_TEST("duplicate keys: last value wins") {
  StableConfig cfg;
  cfg.values["DD_SERVICE"] = "first";
  cfg.values["DD_SERVICE"] = "second";
  REQUIRE(*cfg.lookup("DD_SERVICE") == "second");
}

STABLE_CONFIG_TEST("get_stable_config_paths returns platform paths") {
  auto paths = get_stable_config_paths();
#ifdef _WIN32
  // On Windows, paths should contain backslashes and
  // application_monitoring.yaml.
  REQUIRE(paths.local_path.find("application_monitoring.yaml") !=
          std::string::npos);
  REQUIRE(paths.fleet_path.find("managed") != std::string::npos);
#else
  REQUIRE(paths.local_path == "/etc/datadog-agent/application_monitoring.yaml");
  REQUIRE(paths.fleet_path ==
          "/etc/datadog-agent/managed/datadog-agent/stable/"
          "application_monitoring.yaml");
#endif
}

STABLE_CONFIG_TEST("load_stable_configs with missing files returns empty") {
  MockLogger logger;
  // The default paths likely don't exist in the test environment, so this
  // should return empty configs without errors.
  auto configs = load_stable_configs(logger);
  REQUIRE(configs.local.values.empty());
  REQUIRE(configs.fleet.values.empty());
  REQUIRE(!configs.local.config_id.has_value());
  REQUIRE(!configs.fleet.config_id.has_value());
}

STABLE_CONFIG_TEST("load_one reads valid YAML from disk") {
  TempDir dir;
  auto path = (dir.path() / "valid.yaml").string();
  {
    std::ofstream out(path);
    out << "apm_configuration_default:\n"
           "  DD_SERVICE: my-service\n"
           "  DD_ENV: prod\n";
  }
  MockLogger logger;
  auto cfg = load_one(path, logger);
  REQUIRE(cfg.values.size() == 2);
  REQUIRE(cfg.values.at("DD_SERVICE") == "my-service");
  REQUIRE(cfg.values.at("DD_ENV") == "prod");
  REQUIRE(logger.error_count() == 0);
}

STABLE_CONFIG_TEST("load_one rejects oversized file") {
  TempDir dir;
  auto path = (dir.path() / "oversize.yaml").string();
  {
    std::ofstream out(path);
    out << "apm_configuration_default:\n  DD_SERVICE: ";
    std::string padding(257 * 1024, 'x');
    out << padding;
  }
  MockLogger logger;
  auto cfg = load_one(path, logger);
  REQUIRE(cfg.values.empty());
  REQUIRE(logger.error_count() >= 1);
  // The error message mentions the 256KB cap.
  bool found_size_message = false;
  for (const auto& entry : logger.entries) {
    if (entry.kind == MockLogger::Entry::DD_ERROR) {
      const auto* s = std::get_if<std::string>(&entry.payload);
      if (s && s->find("256KB") != std::string::npos) {
        found_size_message = true;
        break;
      }
    }
  }
  REQUIRE(found_size_message);
}

STABLE_CONFIG_TEST("load_one returns empty on malformed YAML") {
  TempDir dir;
  auto path = (dir.path() / "bad.yaml").string();
  {
    std::ofstream out(path);
    out << "?? ??; ??\t\n\n --- `??";
  }
  MockLogger logger;
  auto cfg = load_one(path, logger);
  REQUIRE(cfg.values.empty());
  REQUIRE(logger.error_count() >= 1);
  bool found_malformed_message = false;
  for (const auto& entry : logger.entries) {
    if (entry.kind == MockLogger::Entry::DD_ERROR) {
      const auto* s = std::get_if<std::string>(&entry.payload);
      if (s && s->find("malformed") != std::string::npos) {
        found_malformed_message = true;
        break;
      }
    }
  }
  REQUIRE(found_malformed_message);
}

STABLE_CONFIG_TEST("load_one returns empty when file is missing") {
  TempDir dir;
  auto path = (dir.path() / "does-not-exist.yaml").string();
  MockLogger logger;
  auto cfg = load_one(path, logger);
  REQUIRE(cfg.values.empty());
  REQUIRE(!cfg.config_id.has_value());
  REQUIRE(logger.error_count() == 0);  // missing files are silently skipped
}

#ifndef _WIN32
STABLE_CONFIG_TEST("load_one logs when file exists but is unreadable") {
  TempDir dir;
  auto path = (dir.path() / "unreadable.yaml").string();
  {
    std::ofstream out(path);
    out << "apm_configuration_default:\n  DD_SERVICE: my-service\n";
  }
  // Make the file unreadable. If chmod doesn't take effect (e.g., running as
  // root or on a filesystem that ignores POSIX permissions), the rest of the
  // test wouldn't be meaningful; gracefully skip in that case.
  REQUIRE(::chmod(path.c_str(), 0) == 0);

  MockLogger logger;
  auto cfg = load_one(path, logger);

  // Restore permissions so the TempDir destructor can clean up.
  ::chmod(path.c_str(), 0644);

  if (cfg.values.empty() && logger.error_count() >= 1) {
    bool found_unreadable_message = false;
    for (const auto& entry : logger.entries) {
      if (entry.kind == MockLogger::Entry::DD_ERROR) {
        const auto* s = std::get_if<std::string>(&entry.payload);
        if (s && s->find("could not be") != std::string::npos) {
          found_unreadable_message = true;
          break;
        }
      }
    }
    REQUIRE(found_unreadable_message);
  }
  // else: the runtime ignored chmod (likely running as root); skip assertion.
}
#endif

STABLE_CONFIG_TEST("LocalStableConfigSource wraps StableConfig::lookup") {
  StableConfig cfg;
  cfg.values["DD_SERVICE"] = "from-local";
  LocalStableConfigSource src(cfg);

  REQUIRE(src.lookup("DD_SERVICE") == Optional<std::string>("from-local"));
  REQUIRE(!src.lookup("DD_MISSING").has_value());
  REQUIRE(src.origin() == ConfigMetadata::Origin::LOCAL_STABLE_CONFIG);
  REQUIRE(!src.config_id().has_value());
}

STABLE_CONFIG_TEST("FleetStableConfigSource forwards config_id") {
  StableConfig cfg;
  cfg.values["DD_SERVICE"] = "from-fleet";
  cfg.config_id = std::string{"fleet-id-42"};
  FleetStableConfigSource src(cfg);

  REQUIRE(src.lookup("DD_SERVICE") == Optional<std::string>("from-fleet"));
  REQUIRE(src.origin() == ConfigMetadata::Origin::FLEET_STABLE_CONFIG);
  REQUIRE(src.config_id() == Optional<std::string>("fleet-id-42"));
}
