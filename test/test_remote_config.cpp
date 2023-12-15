#include <iostream>

#include "catch.hpp"
#include "datadog/json_fwd.hpp"
#include "datadog/remote_config.h"
#include "datadog/trace_sampler_config.h"
#include "mocks/loggers.h"
#include "test.h"

using namespace datadog::tracing;

#define REMOTE_CONFIG_TEST(x) TEST_CASE(x, "[remote_config]")

REMOTE_CONFIG_TEST("first payload") {
  const TracerID tracer_id{/* runtime_id = */ RuntimeID::generate(),
                           /* service = */ "testsvc",
                           /* environment = */ "test"};

  TracerConfig tracer_cfg;

  const std::time_t mock_time = 1672484400;
  const Clock clock = []() {
    TimePoint result;
    result.wall = std::chrono::system_clock::from_time_t(mock_time);
    return result;
  };

  TracerConfig config;
  config.set_service_name("testsvc");
  config.set_environment("test");
  ConfigManager config_manager(*config.finalize());

  RemoteConfigurationManager rc(tracer_id, config_manager);

  const auto payload = rc.make_request_payload();

  CHECK(payload.contains("error") == false);
  CHECK(payload["client"]["is_tracer"] == true);
  CHECK(payload["client"]["client_tracer"]["language"] == "cpp");
  CHECK(payload["client"]["client_tracer"]["service"] == "testsvc");
  CHECK(payload["client"]["client_tracer"]["env"] == "test");
  CHECK(payload["client"]["state"]["root_version"] == 1);
  CHECK(payload["client"]["state"]["targets_version"] == 1);
}

REMOTE_CONFIG_TEST("response processing") {
  const TracerID tracer_id{/* runtime_id = */ RuntimeID::generate(),
                           /* service = */ "testsvc",
                           /* environment = */ "test"};

  TracerConfig tracer_cfg;

  const std::time_t mock_time = 1672484400;
  const Clock clock = []() {
    TimePoint result;
    result.wall = std::chrono::system_clock::from_time_t(mock_time);
    return result;
  };

  TracerConfig config;
  config.set_service_name("testsvc");
  config.set_environment("test");

  TraceSamplerConfig trace_sampler;
  trace_sampler.sample_rate = 1.0;

  config.set_trace_sampler(trace_sampler);
  ConfigManager config_manager(*config.finalize());

  RemoteConfigurationManager rc(tracer_id, config_manager);

  SECTION("ill formatted input",
          "inputs not following the Remote Configuration JSON schema should "
          "generate an error") {
    // clang-format off
    auto test_case = GENERATE(values<std::string>({
      // Missing all fields
      "{}",
      // `targets` field is empty
      R"({ "targets": "" })",
      // `targets` field is not base64 encoded
      R"({ "targets": "Hello, Mars!" })",
      // `targets` field is not a JSON base64 encoded
      // decode("bm90IGpzb24=") == "not json"
      R"({ "targets": "bm90IGpzb24=" })",
      // `targets` field JSON base64 encoded do not follow the expected schema
      // decode("eyJmb28iOiAiYmFyIn0=") == "{"foo": "bar"}"
      R"({ "targets": "eyJmb28iOiAiYmFyIn0=" })",
      // `targets` is missing the `targets` field.
      // decode("eyJzaWduZWQiOiB7InZlcnNpb24iOiAyLCAiY3VzdG9tIjogeyJvcGFxdWVfYmFja2VuZF9zdGF0ZSI6ICIxNSJ9fX0=") == "{"signed": {"version": 2, "custom": {"opaque_backend_state": "15"}}}"
      R"({ 
          "targets": "eyJzaWduZWQiOiB7InZlcnNpb24iOiAyLCAiY3VzdG9tIjogeyJvcGFxdWVfYmFja2VuZF9zdGF0ZSI6ICIxNSJ9fX0=",
          "client_configs": ["datadog"]
      })",
      // `/targets/targets` have no `datadog` entry
      // {"signed": {"version": 2, "targets": {"foo": {}, "bar": {}},"custom": {"opaque_backend_state": "15"}}}
      R"({ 
          "targets": "eyJzaWduZWQiOiB7InZlcnNpb24iOiAyLCAidGFyZ2V0cyI6IHsiZm9vIjoge30sICJiYXIiOiB7fX0sImN1c3RvbSI6IHsib3BhcXVlX2JhY2tlbmRfc3RhdGUiOiAiMTUifX19",
          "client_configs": ["datadog"]
      })",
      // `targets` OK but no `target_files` field.
      // {"signed": {"version": 2, "targets": {"foo/APM_TRACING/30": {}, "bar": {}},"custom": {"opaque_backend_state": "15"}}}
      R"({ 
          "targets": "eyJzaWduZWQiOiB7InZlcnNpb24iOiAyLCAidGFyZ2V0cyI6IHsiZm9vL0FQTV9UUkFDSU5HLzMwIjoge30sICJiYXIiOiB7fX0sImN1c3RvbSI6IHsib3BhcXVlX2JhY2tlbmRfc3RhdGUiOiAiMTUifX19",
          "client_configs": ["foo/APM_TRACING/30"]
      })",
      // `targets` OK. `target_files` field is empty.
      // {"signed": {"version": 2, "targets": {"foo/APM_TRACING/30": {}, "bar": {}},"custom": {"opaque_backend_state": "15"}}}
      R"({ 
          "targets": "eyJzaWduZWQiOiB7InZlcnNpb24iOiAyLCAidGFyZ2V0cyI6IHsiZm9vL0FQTV9UUkFDSU5HLzMwIjoge30sICJiYXIiOiB7fX0sImN1c3RvbSI6IHsib3BhcXVlX2JhY2tlbmRfc3RhdGUiOiAiMTUifX19",
          "client_configs": ["foo/APM_TRACING/30"],
          "target_files": []
      })",
      // `targets` OK. `target_files` field is not an array.
      // {"signed": {"version": 2, "targets": {"foo/APM_TRACING/30": {}, "bar": {}},"custom": {"opaque_backend_state": "15"}}}
      R"({ 
          "targets": "eyJzaWduZWQiOiB7InZlcnNpb24iOiAyLCAidGFyZ2V0cyI6IHsiZm9vL0FQTV9UUkFDSU5HLzMwIjoge30sICJiYXIiOiB7fX0sImN1c3RvbSI6IHsib3BhcXVlX2JhY2tlbmRfc3RhdGUiOiAiMTUifX19",
          "client_configs": ["foo/APM_TRACING/30"],
          "target_files": 15
      })",
      // `targets` OK. `target_files` field content is not base64 encoded.
      // {"signed": {"version": 2, "targets": {"foo/APM_TRACING/30": {}, "bar": {}},"custom": {"opaque_backend_state": "15"}}}
      R"({ 
          "targets": "eyJzaWduZWQiOiB7InZlcnNpb24iOiAyLCAidGFyZ2V0cyI6IHsiZm9vL0FQTV9UUkFDSU5HLzMwIjoge30sICJiYXIiOiB7fX0sImN1c3RvbSI6IHsib3BhcXVlX2JhY2tlbmRfc3RhdGUiOiAiMTUifX19",
          "client_configs": ["foo/APM_TRACING/30"],
          "target_files": [{"path": "foo/APM_TRACING/30", "raw": "Hello, Uranus!"}]
      })",
      // `targets` OK. `target_files` field content is not a JSON base64 encoded.
      // decode("bm90IGpzb24=") == "not json"
      // {"signed": {"version": 2, "targets": {"foo/APM_TRACING/30": {}, "bar": {}},"custom": {"opaque_backend_state": "15"}}}
      R"({ 
          "targets": "eyJzaWduZWQiOiB7InZlcnNpb24iOiAyLCAidGFyZ2V0cyI6IHsiZm9vL0FQTV9UUkFDSU5HLzMwIjoge30sICJiYXIiOiB7fX0sImN1c3RvbSI6IHsib3BhcXVlX2JhY2tlbmRfc3RhdGUiOiAiMTUifX19",
          "client_configs": ["foo/APM_TRACING/30"],
          "target_files": [{"path": "foo/APM_TRACING/30", "raw": "bm90IGpzb24="}]
      })",
      // `targets` OK. `target_files` field JSON base64 content do not follow the expected schema.
      // decode("eyJmb28iOiAiYmFyIn0=") == "{"foo": "bar"}"
      // {"signed": {"version": 2, "targets": {"foo/APM_TRACING/30": {}, "bar": {}},"custom": {"opaque_backend_state": "15"}}}
      R"({ 
          "targets": "eyJzaWduZWQiOiB7InZlcnNpb24iOiAyLCAidGFyZ2V0cyI6IHsiZm9vL0FQTV9UUkFDSU5HLzMwIjoge30sICJiYXIiOiB7fX0sImN1c3RvbSI6IHsib3BhcXVlX2JhY2tlbmRfc3RhdGUiOiAiMTUifX19",
          "client_configs": ["foo/APM_TRACING/30"],
          "target_files": [{"path": "foo/APM_TRACING/30", "raw": "eyJmb28iOiAiYmFyIn0="}]
      })",
    }));
    // clang-format on

    CAPTURE(test_case);
    const auto response_json =
        nlohmann::json::parse(/* input = */ test_case,
                              /* parser_callback = */ nullptr,
                              /* allow_exceptions = */ false);

    REQUIRE(!response_json.is_discarded());
    rc.process_response(response_json);

    // Next payload should contains an error.
    const auto payload = rc.make_request_payload();
    CHECK(payload.contains("error") == true);
    CHECK(payload.contains("has_error") == true);
  }

  SECTION("valid remote configuration") {
    // clang-format off
    const std::string json_input = R"({
      "targets": "ewogICAgInNpZ25lZCI6IHsKICAgICAgICAiY3VzdG9tIjogewogICAgICAgICAgICAiYWdlbnRfcmVmcmVzaF9pbnRlcnZhbCI6IDUsCiAgICAgICAgICAgICJvcGFxdWVfYmFja2VuZF9zdGF0ZSI6ICJleUoyWlhKemFXOXVJam95TENKemRHRjBaU0k2ZXlKbWFXeGxYMmhoYzJobGN5STZleUprWVhSaFpHOW5MekV3TURBeE1qVTROREF2UVZCTlgxUlNRVU5KVGtjdk9ESTNaV0ZqWmpoa1ltTXpZV0l4TkRNMFpETXlNV05pT0RGa1ptSm1OMkZtWlRZMU5HRTBZall4TVRGalpqRTJOakJpTnpGalkyWTRPVGM0TVRrek9DOHlPVEE0Tm1Ka1ltVTFNRFpsTmpoaU5UQm1NekExTlRneU0yRXpaR0UxWTJVd05USTRaakUyTkRCa05USmpaamc0TmpFNE1UWmhZV0U1Wm1ObFlXWTBJanBiSW05WVpESnBlVU16ZUM5b1JXc3hlWFZoWTFoR04xbHFjWEpwVGs5QldVdHVaekZ0V0UwMU5WWktUSGM5SWwxOWZYMD0iCiAgICAgICAgfSwKICAgICAgICAic3BlY192ZXJzaW9uIjogIjEuMC4wIiwKICAgICAgICAidGFyZ2V0cyI6IHsKICAgICAgICAgICAgImZvby9BUE1fVFJBQ0lORy8zMCI6IHsKICAgICAgICAgICAgICAgICJoYXNoZXMiOiB7CiAgICAgICAgICAgICAgICAgICAgInNoYTI1NiI6ICJhMTc3NzY4YjIwYjdjN2Y4NDQ5MzVjYWU2OWM1YzVlZDg4ZWFhZTIzNGUwMTgyYTc4MzU5OTczMzllNTUyNGJjIgogICAgICAgICAgICAgICAgfSwKICAgICAgICAgICAgICAgICJsZW5ndGgiOiAzNzQKICAgICAgICAgICAgfQogICAgICAgIH0sCiAgICAgICAgInZlcnNpb24iOiA2NjIwNDMyMAogICAgfQp9",
      "client_configs": ["foo/APM_TRACING/30"],
      "target_files": [
        {
          "path": "foo/APM_TRACING/30",
          "raw": "eyAiaWQiOiAiODI3ZWFjZjhkYmMzYWIxNDM0ZDMyMWNiODFkZmJmN2FmZTY1NGE0YjYxMTFjZjE2NjBiNzFjY2Y4OTc4MTkzOCIsICJyZXZpc2lvbiI6IDE2OTgxNjcxMjYwNjQsICJzY2hlbWFfdmVyc2lvbiI6ICJ2MS4wLjAiLCAiYWN0aW9uIjogImVuYWJsZSIsICJsaWJfY29uZmlnIjogeyAibGlicmFyeV9sYW5ndWFnZSI6ICJhbGwiLCAibGlicmFyeV92ZXJzaW9uIjogImxhdGVzdCIsICJzZXJ2aWNlX25hbWUiOiAidGVzdHN2YyIsICJlbnYiOiAidGVzdCIsICJ0cmFjaW5nX2VuYWJsZWQiOiB0cnVlLCAidHJhY2luZ19zYW1wbGluZ19yYXRlIjogMC42IH0sICJzZXJ2aWNlX3RhcmdldCI6IHsgInNlcnZpY2UiOiAidGVzdHN2YyIsICJlbnYiOiAidGVzdCIgfSB9"
        }
      ]
    })";
    // clang-format on

    const auto response_json =
        nlohmann::json::parse(/* input = */ json_input,
                              /* parser_callback = */ nullptr,
                              /* allow_exceptions = */ false);

    REQUIRE(!response_json.is_discarded());

    const auto old_trace_sampler = config_manager.get_trace_sampler();
    rc.process_response(response_json);
    const auto new_trace_sampler = config_manager.get_trace_sampler();

    CHECK(new_trace_sampler != old_trace_sampler);

    SECTION("reset confguration") {
      SECTION(
          "missing from client_configs -> all configurations should be reset") {
        // clang-format off
        const std::string json_input = R"({
          "targets": "ewogICAgInNpZ25lZCI6IHsKICAgICAgICAiY3VzdG9tIjogewogICAgICAgICAgICAiYWdlbnRfcmVmcmVzaF9pbnRlcnZhbCI6IDUsCiAgICAgICAgICAgICJvcGFxdWVfYmFja2VuZF9zdGF0ZSI6ICJleUoyWlhKemFXOXVJam95TENKemRHRjBaU0k2ZXlKbWFXeGxYMmhoYzJobGN5STZleUprWVhSaFpHOW5MekV3TURBeE1qVTROREF2UVZCTlgxUlNRVU5KVGtjdk9ESTNaV0ZqWmpoa1ltTXpZV0l4TkRNMFpETXlNV05pT0RGa1ptSm1OMkZtWlRZMU5HRTBZall4TVRGalpqRTJOakJpTnpGalkyWTRPVGM0TVRrek9DOHlPVEE0Tm1Ka1ltVTFNRFpsTmpoaU5UQm1NekExTlRneU0yRXpaR0UxWTJVd05USTRaakUyTkRCa05USmpaamc0TmpFNE1UWmhZV0U1Wm1ObFlXWTBJanBiSW05WVpESnBlVU16ZUM5b1JXc3hlWFZoWTFoR04xbHFjWEpwVGs5QldVdHVaekZ0V0UwMU5WWktUSGM5SWwxOWZYMD0iCiAgICAgICAgfSwKICAgICAgICAic3BlY192ZXJzaW9uIjogIjEuMC4wIiwKICAgICAgICAidGFyZ2V0cyI6IHsKICAgICAgICAgICAgImZvby9BUE1fVFJBQ0lORy8zMCI6IHsKICAgICAgICAgICAgICAgICJoYXNoZXMiOiB7CiAgICAgICAgICAgICAgICAgICAgInNoYTI1NiI6ICJhMTc3NzY4YjIwYjdjN2Y4NDQ5MzVjYWU2OWM1YzVlZDg4ZWFhZTIzNGUwMTgyYTc4MzU5OTczMzllNTUyNGJjIgogICAgICAgICAgICAgICAgfSwKICAgICAgICAgICAgICAgICJsZW5ndGgiOiAzNzQKICAgICAgICAgICAgfQogICAgICAgIH0sCiAgICAgICAgInZlcnNpb24iOiA2NjIwNDMyMAogICAgfQp9",
          "target_files": []
        })";
        // clang-format on

        const auto response_json =
            nlohmann::json::parse(/* input = */ json_input,
                                  /* parser_callback = */ nullptr,
                                  /* allow_exceptions = */ false);

        REQUIRE(!response_json.is_discarded());

        rc.process_response(response_json);
        const auto current_trace_sampler = config_manager.get_trace_sampler();
        CHECK(old_trace_sampler == current_trace_sampler);
      }

      SECTION("missing configuration field -> field should be reset") {
        // clang-format off
        const std::string json_input = R"({
          "targets": "ewogICAgInNpZ25lZCI6IHsKICAgICAgICAiY3VzdG9tIjogewogICAgICAgICAgICAiYWdlbnRfcmVmcmVzaF9pbnRlcnZhbCI6IDUsCiAgICAgICAgICAgICJvcGFxdWVfYmFja2VuZF9zdGF0ZSI6ICJleUoyWlhKemFXOXVJam95TENKemRHRjBaU0k2ZXlKbWFXeGxYMmhoYzJobGN5STZleUprWVhSaFpHOW5MekV3TURBeE1qVTROREF2UVZCTlgxUlNRVU5KVGtjdk9ESTNaV0ZqWmpoa1ltTXpZV0l4TkRNMFpETXlNV05pT0RGa1ptSm1OMkZtWlRZMU5HRTBZall4TVRGalpqRTJOakJpTnpGalkyWTRPVGM0TVRrek9DOHlPVEE0Tm1Ka1ltVTFNRFpsTmpoaU5UQm1NekExTlRneU0yRXpaR0UxWTJVd05USTRaakUyTkRCa05USmpaamc0TmpFNE1UWmhZV0U1Wm1ObFlXWTBJanBiSW05WVpESnBlVU16ZUM5b1JXc3hlWFZoWTFoR04xbHFjWEpwVGs5QldVdHVaekZ0V0UwMU5WWktUSGM5SWwxOWZYMD0iCiAgICAgICAgfSwKICAgICAgICAic3BlY192ZXJzaW9uIjogIjEuMC4wIiwKICAgICAgICAidGFyZ2V0cyI6IHsKICAgICAgICAgICAgImZvby9BUE1fVFJBQ0lORy8zMCI6IHsKICAgICAgICAgICAgICAgICJoYXNoZXMiOiB7CiAgICAgICAgICAgICAgICAgICAgInNoYTI1NiI6ICI2OWUzNDZiNWZmY2U4NDVlMjk5ODRlNzU5YjcxZDdiMDdjNTYxOTc5ZmFlOWU4MmVlZDA4MmMwMzhkODZlNmIwIgogICAgICAgICAgICAgICAgfSwKICAgICAgICAgICAgICAgICJsZW5ndGgiOiAzNzQKICAgICAgICAgICAgfQogICAgICAgIH0sCiAgICAgICAgInZlcnNpb24iOiA2NjIwNDMyMAogICAgfQp9",
          "client_configs": ["foo/APM_TRACING/30"],
          "target_files": [
            {
              "path": "foo/APM_TRACING/30",
              "raw": "eyAiaWQiOiAiODI3ZWFjZjhkYmMzYWIxNDM0ZDMyMWNiODFkZmJmN2FmZTY1NGE0YjYxMTFjZjE2NjBiNzFjY2Y4OTc4MTkzOCIsICJyZXZpc2lvbiI6IDE2OTgxNjcxMjYwNjQsICJzY2hlbWFfdmVyc2lvbiI6ICJ2MS4wLjAiLCAiYWN0aW9uIjogImVuYWJsZSIsICJsaWJfY29uZmlnIjogeyAibGlicmFyeV9sYW5ndWFnZSI6ICJhbGwiLCAibGlicmFyeV92ZXJzaW9uIjogImxhdGVzdCIsICJzZXJ2aWNlX25hbWUiOiAidGVzdHN2YyIsICJlbnYiOiAidGVzdCIsICJ0cmFjaW5nX2VuYWJsZWQiOiB0cnVlIH0sICJzZXJ2aWNlX3RhcmdldCI6IHsgInNlcnZpY2UiOiAidGVzdHN2YyIsICJlbnYiOiAidGVzdCIgfSB9"
            }
          ]
        })";
        // clang-format on

        const auto response_json =
            nlohmann::json::parse(/* input = */ json_input,
                                  /* parser_callback = */ nullptr,
                                  /* allow_exceptions = */ false);

        REQUIRE(!response_json.is_discarded());

        rc.process_response(response_json);
        const auto current_trace_sampler = config_manager.get_trace_sampler();
        CHECK(old_trace_sampler == current_trace_sampler);
      }
    }
  }

  SECTION("update received not for us") {
    // clang-format off
    auto test_case = GENERATE(values<std::string>({
      // "service_target": { "service": "not-testsvc", "env": "test" }
      R"({
        "targets": "ewogICAgInNpZ25lZCI6IHsKICAgICAgICAiY3VzdG9tIjogewogICAgICAgICAgICAiYWdlbnRfcmVmcmVzaF9pbnRlcnZhbCI6IDUsCiAgICAgICAgICAgICJvcGFxdWVfYmFja2VuZF9zdGF0ZSI6ICJleUoyWlhKemFXOXVJam95TENKemRHRjBaU0k2ZXlKbWFXeGxYMmhoYzJobGN5STZleUprWVhSaFpHOW5MekV3TURBeE1qVTROREF2UVZCTlgxUlNRVU5KVGtjdk9ESTNaV0ZqWmpoa1ltTXpZV0l4TkRNMFpETXlNV05pT0RGa1ptSm1OMkZtWlRZMU5HRTBZall4TVRGalpqRTJOakJpTnpGalkyWTRPVGM0TVRrek9DOHlPVEE0Tm1Ka1ltVTFNRFpsTmpoaU5UQm1NekExTlRneU0yRXpaR0UxWTJVd05USTRaakUyTkRCa05USmpaamc0TmpFNE1UWmhZV0U1Wm1ObFlXWTBJanBiSW05WVpESnBlVU16ZUM5b1JXc3hlWFZoWTFoR04xbHFjWEpwVGs5QldVdHVaekZ0V0UwMU5WWktUSGM5SWwxOWZYMD0iCiAgICAgICAgfSwKICAgICAgICAic3BlY192ZXJzaW9uIjogIjEuMC4wIiwKICAgICAgICAidGFyZ2V0cyI6IHsKICAgICAgICAgICAgImZvby9BUE1fVFJBQ0lORy8zMCI6IHsKICAgICAgICAgICAgICAgICJoYXNoZXMiOiB7CiAgICAgICAgICAgICAgICAgICAgInNoYTI1NiI6ICJhMTc3NzY4YjIwYjdjN2Y4NDQ5MzVjYWU2OWM1YzVlZDg4ZWFhZTIzNGUwMTgyYTc4MzU5OTczMzllNTUyNGJjIgogICAgICAgICAgICAgICAgfSwKICAgICAgICAgICAgICAgICJsZW5ndGgiOiAzNzQKICAgICAgICAgICAgfQogICAgICAgIH0sCiAgICAgICAgInZlcnNpb24iOiA2NjIwNDMyMAogICAgfQp9",
        "client_configs": ["foo/APM_TRACING/30"],
        "target_files": [
          {
            "path": "foo/APM_TRACING/30",
            "raw": "eyAiaWQiOiAiODI3ZWFjZjhkYmMzYWIxNDM0ZDMyMWNiODFkZmJmN2FmZTY1NGE0YjYxMTFjZjE2NjBiNzFjY2Y4OTc4MTkzOCIsICJyZXZpc2lvbiI6IDE2OTgxNjcxMjYwNjQsICJzY2hlbWFfdmVyc2lvbiI6ICJ2MS4wLjAiLCAiYWN0aW9uIjogImVuYWJsZSIsICJsaWJfY29uZmlnIjogeyAibGlicmFyeV9sYW5ndWFnZSI6ICJhbGwiLCAibGlicmFyeV92ZXJzaW9uIjogImxhdGVzdCIsICJzZXJ2aWNlX25hbWUiOiAidGVzdHN2YyIsICJlbnYiOiAidGVzdCIsICJ0cmFjaW5nX2VuYWJsZWQiOiB0cnVlLCAidHJhY2luZ19zYW1wbGluZ19yYXRlIjogMC42IH0sICJzZXJ2aWNlX3RhcmdldCI6IHsgInNlcnZpY2UiOiAibm90LXRlc3RzdmMiLCAiZW52IjogInRlc3QiIH0gfQ=="
          }
        ]
      })",
      // "service_target": { "service": "testsvc", "env": "dev" }
      R"({
        "targets": "ewogICAgInNpZ25lZCI6IHsKICAgICAgICAiY3VzdG9tIjogewogICAgICAgICAgICAiYWdlbnRfcmVmcmVzaF9pbnRlcnZhbCI6IDUsCiAgICAgICAgICAgICJvcGFxdWVfYmFja2VuZF9zdGF0ZSI6ICJleUoyWlhKemFXOXVJam95TENKemRHRjBaU0k2ZXlKbWFXeGxYMmhoYzJobGN5STZleUprWVhSaFpHOW5MekV3TURBeE1qVTROREF2UVZCTlgxUlNRVU5KVGtjdk9ESTNaV0ZqWmpoa1ltTXpZV0l4TkRNMFpETXlNV05pT0RGa1ptSm1OMkZtWlRZMU5HRTBZall4TVRGalpqRTJOakJpTnpGalkyWTRPVGM0TVRrek9DOHlPVEE0Tm1Ka1ltVTFNRFpsTmpoaU5UQm1NekExTlRneU0yRXpaR0UxWTJVd05USTRaakUyTkRCa05USmpaamc0TmpFNE1UWmhZV0U1Wm1ObFlXWTBJanBiSW05WVpESnBlVU16ZUM5b1JXc3hlWFZoWTFoR04xbHFjWEpwVGs5QldVdHVaekZ0V0UwMU5WWktUSGM5SWwxOWZYMD0iCiAgICAgICAgfSwKICAgICAgICAic3BlY192ZXJzaW9uIjogIjEuMC4wIiwKICAgICAgICAidGFyZ2V0cyI6IHsKICAgICAgICAgICAgImZvby9BUE1fVFJBQ0lORy8zMCI6IHsKICAgICAgICAgICAgICAgICJoYXNoZXMiOiB7CiAgICAgICAgICAgICAgICAgICAgInNoYTI1NiI6ICJhMTc3NzY4YjIwYjdjN2Y4NDQ5MzVjYWU2OWM1YzVlZDg4ZWFhZTIzNGUwMTgyYTc4MzU5OTczMzllNTUyNGJjIgogICAgICAgICAgICAgICAgfSwKICAgICAgICAgICAgICAgICJsZW5ndGgiOiAzNzQKICAgICAgICAgICAgfQogICAgICAgIH0sCiAgICAgICAgInZlcnNpb24iOiA2NjIwNDMyMAogICAgfQp9",
        "client_configs": ["foo/APM_TRACING/30"],
        "target_files": [
          {
            "path": "foo/APM_TRACING/30",
            "raw": "eyAiaWQiOiAiODI3ZWFjZjhkYmMzYWIxNDM0ZDMyMWNiODFkZmJmN2FmZTY1NGE0YjYxMTFjZjE2NjBiNzFjY2Y4OTc4MTkzOCIsICJyZXZpc2lvbiI6IDE2OTgxNjcxMjYwNjQsICJzY2hlbWFfdmVyc2lvbiI6ICJ2MS4wLjAiLCAiYWN0aW9uIjogImVuYWJsZSIsICJsaWJfY29uZmlnIjogeyAibGlicmFyeV9sYW5ndWFnZSI6ICJhbGwiLCAibGlicmFyeV92ZXJzaW9uIjogImxhdGVzdCIsICJzZXJ2aWNlX25hbWUiOiAidGVzdHN2YyIsICJlbnYiOiAidGVzdCIsICJ0cmFjaW5nX2VuYWJsZWQiOiB0cnVlLCAidHJhY2luZ19zYW1wbGluZ19yYXRlIjogMC42IH0sICJzZXJ2aWNlX3RhcmdldCI6IHsgInNlcnZpY2UiOiAidGVzdHN2YyIsICJlbnYiOiAiZGV2IiB9IH0="
          }
        ]
      })"
    }));
    // clang-format on

    CAPTURE(test_case);

    const auto response_json =
        nlohmann::json::parse(/* input = */ test_case,
                              /* parser_callback = */ nullptr,
                              /* allow_exceptions = */ false);

    REQUIRE(!response_json.is_discarded());

    const auto old_sampling_rate = config_manager.get_trace_sampler();
    rc.process_response(response_json);
    const auto new_sampling_rate = config_manager.get_trace_sampler();

    CHECK(new_sampling_rate == old_sampling_rate);
  }
}
