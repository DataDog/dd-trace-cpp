#include <datadog/optional.h>
#include <datadog/tracer_config.h>

#include <cstddef>
#include <filesystem>
#include <string>
#include <unordered_map>

#include "common/environment.h"
#include "mocks/loggers.h"
#include "stable_config.h"
#include "test.h"
#include "yaml_parser.h"

using namespace datadog::test;
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
  DD_TRACE_SAMPLE_RATE: 0.5
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
  REQUIRE(parsed.values.count("DD_TRACE_SAMPLE_RATE"));
  REQUIRE(parsed.values["DD_TRACE_SAMPLE_RATE"] == "0.5");
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

STABLE_CONFIG_TEST(
    "finalize_config: fleet stable config overrides env and local") {
  // We can't easily write to /etc/datadog-agent/ in tests, so we test
  // the precedence via the resolve_and_record_config function directly.
  // The 5-arg overload handles fleet > env > user > local > default.

  std::unordered_map<ConfigName, std::vector<ConfigMetadata>> metadata;

  Optional<std::string> fleet_val("fleet-service");
  Optional<std::string> env_val("env-service");
  Optional<std::string> user_val("user-service");
  Optional<std::string> local_val("local-service");

  auto result = resolve_and_record_config(
      fleet_val, env_val, user_val, local_val, &metadata,
      ConfigName::SERVICE_NAME, std::string("default-service"));

  // Fleet should win.
  REQUIRE(result == "fleet-service");

  // Check metadata entries: should have all 5 sources in precedence order.
  auto it = metadata.find(ConfigName::SERVICE_NAME);
  REQUIRE(it != metadata.end());
  auto& entries = it->second;
  REQUIRE(entries.size() == 5);

  // Order: default, local_stable, code, env, fleet_stable
  REQUIRE(entries[0].origin == ConfigMetadata::Origin::DEFAULT);
  REQUIRE(entries[0].value == "default-service");
  REQUIRE(entries[1].origin == ConfigMetadata::Origin::LOCAL_STABLE_CONFIG);
  REQUIRE(entries[1].value == "local-service");
  REQUIRE(entries[2].origin == ConfigMetadata::Origin::CODE);
  REQUIRE(entries[2].value == "user-service");
  REQUIRE(entries[3].origin == ConfigMetadata::Origin::ENVIRONMENT_VARIABLE);
  REQUIRE(entries[3].value == "env-service");
  REQUIRE(entries[4].origin == ConfigMetadata::Origin::FLEET_STABLE_CONFIG);
  REQUIRE(entries[4].value == "fleet-service");
}

STABLE_CONFIG_TEST("precedence: env > local_stable") {
  std::unordered_map<ConfigName, std::vector<ConfigMetadata>> metadata;

  Optional<std::string> fleet_val;  // nullopt
  Optional<std::string> env_val("env-service");
  Optional<std::string> user_val;  // nullopt
  Optional<std::string> local_val("local-service");

  auto result = resolve_and_record_config(
      fleet_val, env_val, user_val, local_val, &metadata,
      ConfigName::SERVICE_NAME, std::string("default-service"));

  REQUIRE(result == "env-service");
}

STABLE_CONFIG_TEST("precedence: user > local_stable") {
  std::unordered_map<ConfigName, std::vector<ConfigMetadata>> metadata;

  Optional<std::string> fleet_val;
  Optional<std::string> env_val;
  Optional<std::string> user_val("user-service");
  Optional<std::string> local_val("local-service");

  auto result = resolve_and_record_config(
      fleet_val, env_val, user_val, local_val, &metadata,
      ConfigName::SERVICE_NAME, std::string("default-service"));

  REQUIRE(result == "user-service");
}

STABLE_CONFIG_TEST("precedence: local_stable > default") {
  std::unordered_map<ConfigName, std::vector<ConfigMetadata>> metadata;

  Optional<std::string> fleet_val;
  Optional<std::string> env_val;
  Optional<std::string> user_val;
  Optional<std::string> local_val("local-service");

  auto result = resolve_and_record_config(
      fleet_val, env_val, user_val, local_val, &metadata,
      ConfigName::SERVICE_NAME, std::string("default-service"));

  REQUIRE(result == "local-service");
}

STABLE_CONFIG_TEST("precedence: fleet > env") {
  std::unordered_map<ConfigName, std::vector<ConfigMetadata>> metadata;

  Optional<std::string> fleet_val("fleet-service");
  Optional<std::string> env_val("env-service");
  Optional<std::string> user_val;
  Optional<std::string> local_val;

  auto result =
      resolve_and_record_config(fleet_val, env_val, user_val, local_val,
                                &metadata, ConfigName::SERVICE_NAME);

  REQUIRE(result == "fleet-service");
}

STABLE_CONFIG_TEST("precedence: only default") {
  std::unordered_map<ConfigName, std::vector<ConfigMetadata>> metadata;

  Optional<std::string> fleet_val;
  Optional<std::string> env_val;
  Optional<std::string> user_val;
  Optional<std::string> local_val;

  auto result = resolve_and_record_config(
      fleet_val, env_val, user_val, local_val, &metadata,
      ConfigName::SERVICE_NAME, std::string("default-service"));

  REQUIRE(result == "default-service");
}

STABLE_CONFIG_TEST("precedence: no values yields empty string") {
  std::unordered_map<ConfigName, std::vector<ConfigMetadata>> metadata;

  Optional<std::string> fleet_val;
  Optional<std::string> env_val;
  Optional<std::string> user_val;
  Optional<std::string> local_val;

  auto result =
      resolve_and_record_config(fleet_val, env_val, user_val, local_val,
                                &metadata, ConfigName::SERVICE_NAME);

  REQUIRE(result.empty());
  REQUIRE(metadata.empty());
}

STABLE_CONFIG_TEST("bool precedence: fleet > env > user > local > default") {
  std::unordered_map<ConfigName, std::vector<ConfigMetadata>> metadata;
  auto to_str = [](const bool& b) { return b ? "true" : "false"; };

  SECTION("fleet wins") {
    auto result = resolve_and_record_config(
        Optional<bool>(false), Optional<bool>(true), Optional<bool>(true),
        Optional<bool>(true), &metadata, ConfigName::REPORT_TRACES, true,
        to_str);
    REQUIRE(result == false);
  }

  SECTION("env wins when no fleet") {
    auto result = resolve_and_record_config(
        Optional<bool>(), Optional<bool>(false), Optional<bool>(true),
        Optional<bool>(true), &metadata, ConfigName::REPORT_TRACES, true,
        to_str);
    REQUIRE(result == false);
  }
}

STABLE_CONFIG_TEST("FinalizedTracerConfig stores stable config values") {
  TracerConfig config;
  config.service = "testsvc";

  auto finalized = finalize_config(config);
  REQUIRE(finalized);

  // On a typical dev machine, the stable config files don't exist,
  // so the maps should be empty.
  // (We can't easily create the files at the expected paths in tests.)
  // But the fields should exist and be accessible.
  REQUIRE(finalized->local_stable_config_values.empty());
  REQUIRE(finalized->fleet_stable_config_values.empty());
}
