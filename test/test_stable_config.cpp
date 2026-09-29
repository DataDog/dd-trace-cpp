#include <datadog/stable_config_loader.h>

#include <chrono>
#include <filesystem>
#include <fstream>

#include "common/environment.h"
#include "mocks/http_clients.h"
#include "stable_config_loader_internal.h"
#include "test.h"

using namespace datadog::tracing;
using datadog::test::EnvGuard;

#define STABLE_CONFIG_TEST(name) TEST_CASE(name, "[stable.config]")

STABLE_CONFIG_TEST("stable config observes source precedence") {
  const std::string local = std::string{DD_TRACE_SOURCE_DIR} +
                            "/test/fixtures/stable_config/local.yaml";
  const std::string fleet = std::string{DD_TRACE_SOURCE_DIR} +
                            "/test/fixtures/stable_config/fleet.yaml";
  EnvGuard service_env{"DD_SERVICE", "env-service"};
  EnvGuard environment_env{"DD_ENV", "env-environment"};
  EnvGuard version_env{"DD_VERSION"};
  EnvGuard sample_rate_env{"DD_TRACE_SAMPLE_RATE", "0.75"};
  EnvGuard apm_enabled_env{"DD_APM_TRACING_ENABLED"};
  EnvGuard agent_url_env{"DD_TRACE_AGENT_URL"};
  EnvGuard agent_host_env{"DD_AGENT_HOST", "env-agent"};
  EnvGuard agent_port_env{"DD_TRACE_AGENT_PORT"};

  auto loaded = load_stable_config("cpp", local, fleet);
  REQUIRE(loaded);

  TracerConfig code;
  code.service = "code-service";
  code.agent.http_client = std::make_shared<MockHTTPClient>();
  auto with_code = finalize_config(code, *loaded);
  REQUIRE(with_code);
  CHECK(with_code->defaults.service == "code-service");
  CHECK(with_code->defaults.environment == "env-environment");
  CHECK(with_code->defaults.version == "local-version");
  CHECK(with_code->agent_url.authority == "env-agent:8126");
  CHECK(with_code->metadata.at(ConfigName::AGENT_URL).back().origin ==
        ConfigMetadata::Origin::ENVIRONMENT_VARIABLE);
  REQUIRE(with_code->extraction_styles.size() == 1);
  CHECK(with_code->extraction_styles.front() == PropagationStyle::B3);
  CHECK_FALSE(with_code->tracing_enabled);
  CHECK(with_code->metadata.at(ConfigName::SERVICE_NAME).back().origin ==
        ConfigMetadata::Origin::CODE);
  const auto& sample_rate =
      with_code->metadata.at(ConfigName::TRACE_SAMPLING_RATE).back();
  CHECK(sample_rate.origin == ConfigMetadata::Origin::FLEET_STABLE_CONFIG);
  REQUIRE(sample_rate.config_id);
  CHECK(*sample_rate.config_id == "fleet-test");
  const auto& extraction =
      with_code->metadata.at(ConfigName::EXTRACTION_STYLES).back();
  CHECK(extraction.origin == ConfigMetadata::Origin::LOCAL_STABLE_CONFIG);
  REQUIRE(extraction.config_id);
  CHECK(*extraction.config_id == "local-test");

  code.trace_sampler.sample_rate = 0.9;
  auto with_code_rate = finalize_config(code, *loaded);
  REQUIRE(with_code_rate);
  CHECK(with_code_rate->metadata.at(ConfigName::TRACE_SAMPLING_RATE)
            .back()
            .origin == ConfigMetadata::Origin::CODE);

  TracerConfig no_code;
  no_code.agent.http_client = code.agent.http_client;
  auto without_code = finalize_config(no_code, *loaded);
  REQUIRE(without_code);
  CHECK(without_code->defaults.service == "fleet-service");
  CHECK(without_code->metadata.at(ConfigName::SERVICE_NAME).back().origin ==
        ConfigMetadata::Origin::FLEET_STABLE_CONFIG);
}

STABLE_CONFIG_TEST("missing stable config files are ignored") {
  auto loaded = load_stable_config("cpp", "/nonexistent/local.yaml",
                                   "/nonexistent/fleet.yaml");
  REQUIRE(loaded);
  CHECK_FALSE(loaded->find("DD_SERVICE"));
}

STABLE_CONFIG_TEST("invalid stable config is reported") {
  const std::string invalid = std::string{DD_TRACE_SOURCE_DIR} +
                              "/test/fixtures/stable_config/invalid.yaml";
  auto loaded = load_stable_config("cpp", invalid, "/nonexistent/fleet.yaml");
  CHECK_FALSE(loaded);
}

STABLE_CONFIG_TEST("stable config applies one matching rule per source") {
  stable_config_internal::ProcessInfo process{"cpp", {}, {}};
  auto loaded = stable_config_internal::load_yaml(
      R"(config_id: local
apm_configuration_default:
  DD_SERVICE: local-default
  DD_ENV: local-env
rules:
  - selectors:
      - {origin: language, operator: equals, matches: [java]}
    configuration: {DD_SERVICE: ignored}
  - selectors:
      - {origin: language, operator: equals, matches: [cpp]}
    configuration: {DD_SERVICE: local-rule, DD_VERSION: local-version}
  - selectors: []
    configuration: {DD_SERVICE: later-rule, DD_VERSION: later-version}
)",
      R"(config_id: fleet
apm_configuration_default:
  DD_ENV: fleet-env
rules:
  - selectors: []
    configuration: {DD_SERVICE: fleet-rule}
)",
      process);
  REQUIRE(loaded);
  REQUIRE(loaded->find("DD_SERVICE"));
  CHECK(loaded->find("DD_SERVICE")->value == "fleet-rule");
  CHECK(loaded->find("DD_SERVICE")->source == StableConfigSource::FLEET);
  CHECK(loaded->find("DD_SERVICE")->config_id == "fleet");
  CHECK(loaded->find("DD_ENV")->value == "fleet-env");
  CHECK(loaded->find("DD_VERSION")->value == "local-version");
  CHECK(loaded->find("DD_VERSION")->source == StableConfigSource::LOCAL);
}

STABLE_CONFIG_TEST("stable config selectors use all origins and operators") {
  stable_config_internal::ProcessInfo process{
      "cpp",
      {"--mode=fast", "run", "--mode=final"},
      {"REGION=eu-west", "FLAG", "REGION=us-east"}};
  auto loaded = stable_config_internal::load_yaml(
      R"(tags: {cluster: blue-prod}
rules:
  - selectors:
      - {origin: language, operator: equals, matches: [java, cpp]}
      - {origin: process_arguments, key: --mode, operator: suffix_matches, matches: [final]}
      - {origin: process_arguments, operator: equals, matches: [run]}
      - {origin: environment_variables, key: REGION, operator: prefix_matches, matches: [us-]}
      - {origin: environment_variables, operator: exists}
      - {origin: environment_variables, key: FLAG, operator: exists}
      - {origin: tags, key: cluster, operator: prefix_matches, matches: [blue]}
    configuration: {DD_SERVICE: matched}
)",
      "", process);
  REQUIRE(loaded);
  REQUIRE(loaded->find("DD_SERVICE"));
  CHECK(loaded->find("DD_SERVICE")->value == "matched");

  auto no_match = stable_config_internal::load_yaml(
      R"(rules:
  - selectors:
      - {origin: tags, operator: exists}
    configuration: {DD_SERVICE: wrong}
)",
      "", process);
  REQUIRE(no_match);
  CHECK_FALSE(no_match->find("DD_SERVICE"));
}

STABLE_CONFIG_TEST("stable config templates only matched rule values") {
  stable_config_internal::ProcessInfo process{
      "cpp", {"--name=worker=one"}, {"ZONE=east"}};
  auto loaded = stable_config_internal::load_yaml(
      R"(apm_configuration_default: {DD_ENV: "{{ language }}"}
tags: {cluster: alpha}
rules:
  - selectors: []
    configuration:
      DD_SERVICE: "{{ language }}-{{ tags[cluster] }}-{{ process_arguments[--name] }}-{{ environment_variables[ZONE] }}-{{ missing }}"
      DD_VERSION: "{{ tags[absent] }}"
      DD_TRACE_AGENT_URL: "prefix{{ tags[ cluster ] }}{{ language[ }}suffix"
      DD_TAGS: "{{ tags }}-{{ tags[cluster }}-{{ unknown[key] }}"
)",
      "", process);
  REQUIRE(loaded);
  CHECK(loaded->find("DD_ENV")->value == "{{ language }}");
  CHECK(loaded->find("DD_SERVICE")->value ==
        "cpp-alpha-worker=one-east-UNDEFINED");
  CHECK(loaded->find("DD_VERSION")->value == "UNDEFINED");
  CHECK(loaded->find("DD_TRACE_AGENT_URL")->value == "prefixalphacppsuffix");
  CHECK(loaded->find("DD_TAGS")->value == "UNDEFINED-UNDEFINED-UNDEFINED");

  auto invalid = stable_config_internal::load_yaml(
      "rules: [{selectors: [], configuration: {DD_SERVICE: '{{ broken'}}]", "",
      process);
  CHECK_FALSE(invalid);
}

STABLE_CONFIG_TEST("stable config accepts scalar values and duplicate keys") {
  stable_config_internal::ProcessInfo process{"cpp", {}, {}};
  auto loaded = stable_config_internal::load_yaml(
      R"(unknown_field: ignored
apm_configuration_default:
  DD_SERVICE: first
  DD_SERVICE: second
  DD_TRACE_ENABLED: true
  DD_TRACE_SAMPLE_RATE: 0.25
)",
      "", process);
  REQUIRE(loaded);
  CHECK(loaded->find("DD_SERVICE")->value == "second");
  CHECK(loaded->find("DD_TRACE_ENABLED")->value == "true");
  CHECK(loaded->find("DD_TRACE_SAMPLE_RATE")->value == "0.25");
}

STABLE_CONFIG_TEST("stable config validates YAML fields") {
  stable_config_internal::ProcessInfo process{"cpp", {}, {}};
  const std::vector<std::string> invalid_documents = {
      "[not, a, map]",
      "apm_configuration_default: []",
      "apm_configuration_default: {DD_SERVICE: null}",
      "tags: []",
      "rules: {}",
      "rules: [null]",
      "rules: [{selectors: {}, configuration: {}}]",
      "rules: [{configuration: {DD_SERVICE: x}}]",
      "rules: [{selectors: [], configuration: []}]",
      "rules: [{selectors: [{origin: invalid, operator: exists}], "
      "configuration: {}}]",
      "rules: [{selectors: [{origin: language, operator: invalid}], "
      "configuration: {}}]",
      "rules: [{selectors: [{origin: language, operator: equals}], "
      "configuration: {}}]",
  };
  for (const std::string& document : invalid_documents) {
    INFO(document);
    CHECK_FALSE(stable_config_internal::load_yaml(document, "", process));
  }
  CHECK(stable_config_internal::load_yaml("", "# comment\n", process));
}

STABLE_CONFIG_TEST("stable config skips oversized files and reads fleet") {
  const std::filesystem::path oversized =
      std::filesystem::temp_directory_path() /
      ("dd-trace-cpp-stable-config-" +
       std::to_string(
           std::chrono::steady_clock::now().time_since_epoch().count()));
  {
    std::ofstream file(oversized);
    REQUIRE(file.good());
  }
  std::filesystem::resize_file(oversized, 100 * 1024 * 1024 + 1);
  const std::string fleet = std::string{DD_TRACE_SOURCE_DIR} +
                            "/test/fixtures/stable_config/fleet.yaml";
  auto loaded = load_stable_config("cpp", oversized.string(), fleet);
  std::filesystem::remove(oversized);
  REQUIRE(loaded);
  REQUIRE(loaded->find("DD_SERVICE"));
  CHECK(loaded->find("DD_SERVICE")->value == "fleet-service");
  CHECK(loaded->find("DD_SERVICE")->source == StableConfigSource::FLEET);
}

STABLE_CONFIG_TEST("stable config reports file read errors") {
  auto loaded =
      load_stable_config("cpp", DD_TRACE_SOURCE_DIR, "/nonexistent/fleet.yaml");
  CHECK_FALSE(loaded);
}
