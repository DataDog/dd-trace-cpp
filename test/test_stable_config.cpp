#include <datadog/stable_config_loader.h>

#include "common/environment.h"
#include "mocks/http_clients.h"
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
