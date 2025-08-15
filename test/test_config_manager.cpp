#include "catch.hpp"
#include "datadog/config_manager.h"
#include "datadog/remote_config/listener.h"
#include "datadog/trace_sampler.h"
#include "mocks/http_clients.h"

namespace rc = datadog::remote_config;

using namespace datadog;
using namespace datadog::tracing;

#define CONFIG_MANAGER_TEST(x) TEST_CASE(x, "[config_manager]")

nlohmann::json load_json(std::string_view sv) {
  auto j = nlohmann::json::parse(/* input = */ sv,
                                 /* parser_callback = */ nullptr,
                                 /* allow_exceptions = */ false);
  REQUIRE(!j.is_discarded());
  return j;
}

CONFIG_MANAGER_TEST("remote configuration handling") {
  TracerConfig config;
  config.service = "testsvc";
  config.environment = "test";

  auto final_cfg = *finalize_config(config);

  auto http_client = std::make_shared<MockHTTPClient>();

  // TODO: set mock telemetry
  ConfigManager config_manager(final_cfg);

  rc::Listener::Configuration config_update{/* id = */ "id",
                                            /* path = */ "",
                                            /* content = */ "",
                                            /* version = */ 1,
                                            rc::product::Flag::APM_TRACING};

  SECTION("handling of `tracing_sampling_rate`") {
    // SECTION("invalid value") {
    //   config_update.content = R"({
    //     "lib_config": {
    //       "library_language": "all",
    //       "library_version": "latest",
    //       "service_name": "testsvc",
    //       "env": "test",
    //       "tracing_enabled": false,
    //       "tracing_sampling_rate": 100.0,
    //       "tracing_tags": [
    //          "hello:world",
    //          "foo:bar"
    //       ]
    //     },
    //     "service_target": {
    //        "service": "testsvc",
    //        "env": "test"
    //     }
    //   })";
    //
    //   const auto old_trace_sampler_config =
    //       config_manager.trace_sampler()->config_json();
    //
    //   const auto err = config_manager.on_update(config_update);
    //   CHECK(!err);
    //
    //   const auto new_trace_sampler_config =
    //       config_manager.trace_sampler()->config_json();
    //
    //   CHECK(old_trace_sampler_config == new_trace_sampler_config);
    // }
    SECTION("valid value") {
      config_update.content = R"({
        "lib_config": {
          "library_language": "all",
          "library_version": "latest",
          "service_name": "testsvc",
          "env": "test",
          "tracing_enabled": false,
          "tracing_sampling_rate": 0.6,
          "tracing_tags": [
             "hello:world",
             "foo:bar"
          ]
        },
        "service_target": {
           "service": "testsvc",
           "env": "test"
        }
      })";

      const auto old_trace_sampler_config =
          config_manager.trace_sampler()->config_json();

      const auto err = config_manager.on_update(config_update);
      CHECK(!err);

      const auto new_trace_sampler_config =
          config_manager.trace_sampler()->config_json();

      CHECK(old_trace_sampler_config != new_trace_sampler_config);

      config_manager.on_revert(config_update);

      const auto revert_trace_sampler_config =
          config_manager.trace_sampler()->config_json();

      CHECK(old_trace_sampler_config == revert_trace_sampler_config);
    }
  }

  SECTION("handling of `tracing_tags`") {
    config_update.content = R"({
        "lib_config": {
          "library_language": "all",
          "library_version": "latest",
          "service_name": "testsvc",
          "env": "test",
          "tracing_enabled": false,
          "tracing_sampling_rate": 0.6,
          "tracing_tags": [
             "hello:world",
             "foo:bar"
          ]
        },
        "service_target": {
           "service": "testsvc",
           "env": "test"
        }
      })";

    const std::unordered_map<std::string, std::string> expected_tags{
        {"hello", "world"}, {"foo", "bar"}};

    const auto old_tags = config_manager.span_defaults()->tags;

    const auto err = config_manager.on_update(config_update);
    CHECK(!err);

    const auto new_tags = config_manager.span_defaults()->tags;

    CHECK(old_tags != new_tags);
    CHECK(new_tags == expected_tags);

    config_manager.on_revert(config_update);

    const auto reverted_tags = config_manager.span_defaults()->tags;

    CHECK(old_tags == reverted_tags);
  }

  SECTION("handling of `tracing_enabled`") {
    config_update.content = R"({
        "lib_config": {
          "library_language": "all",
          "library_version": "latest",
          "service_name": "testsvc",
          "env": "test",
          "tracing_enabled": false,
          "tracing_sampling_rate": 0.6,
          "tracing_tags": [
             "hello:world",
             "foo:bar"
          ]
        },
        "service_target": {
           "service": "testsvc",
           "env": "test"
        }
      })";

    const auto old_tracing_status = config_manager.report_traces();

    const auto err = config_manager.on_update(config_update);
    CHECK(!err);

    const auto new_tracing_status = config_manager.report_traces();

    CHECK(old_tracing_status != new_tracing_status);
    CHECK(new_tracing_status == false);

    config_manager.on_revert(config_update);

    const auto reverted_tracing_status = config_manager.report_traces();
    CHECK(old_tracing_status == reverted_tracing_status);
  }

  SECTION("handling of `tracing_sampling_rules`") {
    SECTION("valid input") {
      config_update.content = R"({
        "lib_config": {
          "library_language": "all",
          "library_version": "latest",
          "service_name": "testsvc",
          "env": "test",
          "tracing_sampling_rules": [
            {
              "service": "foo",
              "resource": "GET /hello",
              "sample_rate": 0.1,
              "provenance": "customer",
              "name": "test",
              "tags": [
                { "key": "tag1", "value_glob": "value1" }
              ]
            }
          ]
        },
        "service_target": {
           "service": "testsvc",
           "env": "test"
        }
      })";

      const auto old_sampler_cfg =
          config_manager.trace_sampler()->config_json();
      const auto err = config_manager.on_update(config_update);
      const auto new_sampler_cfg =
          config_manager.trace_sampler()->config_json();

      CHECK(old_sampler_cfg != new_sampler_cfg);
    }
  }
}
