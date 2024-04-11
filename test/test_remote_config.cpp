#include <iostream>

#include "catch.hpp"
#include "datadog/config_manager.h"
#include "datadog/json_fwd.hpp"
#include "datadog/logger.h"
#include "datadog/remote_config.h"
#include "datadog/tracer_config.h"
#include "mocks/loggers.h"
#include "test.h"

using namespace datadog::tracing;

#define REMOTE_CONFIG_TEST(x) TEST_CASE(x, "[remote_config]")

REMOTE_CONFIG_TEST("first payload") {
  const TracerSignature tracer_signature{
      /* runtime_id = */ RuntimeID::generate(),
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
  config.service = "testsvc";
  config.environment = "test";
  const auto config_manager =
      std::make_shared<ConfigManager>(*finalize_config(config));

  const auto logger =
      std::make_shared<MockLogger>(std::cerr, MockLogger::ERRORS_ONLY);

  RemoteConfigurationManager rc(tracer_signature, config_manager, logger);

  const auto payload = rc.make_request_payload();

  CHECK(payload.contains("error") == false);
  CHECK(payload["client"]["is_tracer"] == true);
  CHECK(payload["client"]["capabilities"] ==
        std::vector{0, 0, 0, 0, 0, 8, 144, 0});
  CHECK(payload["client"]["products"] ==
        std::vector<std::string>{"APM_TRACING"});
  CHECK(payload["client"]["client_tracer"]["language"] == "cpp");
  CHECK(payload["client"]["client_tracer"]["service"] == "testsvc");
  CHECK(payload["client"]["client_tracer"]["env"] == "test");
  CHECK(payload["client"]["state"]["root_version"] == 1);
  // [RFC] Integrating with Remote Config in a Tracer indicates default is 0
  CHECK(payload["client"]["state"]["targets_version"] == 0);

  CHECK(payload["cached_target_files"] == nlohmann::json(nullptr));
}

REMOTE_CONFIG_TEST("response processing") {
  const TracerSignature tracer_signature{
      /* runtime_id = */ RuntimeID::generate(),
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
  config.service = "testsvc";
  config.environment = "test";
  config.trace_sampler.sample_rate = 1.0;
  config.report_traces = true;
  const auto config_manager =
      std::make_shared<ConfigManager>(*finalize_config(config));

  const auto logger =
      std::make_shared<MockLogger>(std::cerr, MockLogger::ERRORS_ONLY);

  RemoteConfigurationManager rc(tracer_signature, config_manager, logger);

  SECTION("empty response") {
    auto test_case =
        GENERATE(values<std::string_view>({"{}", R"({ "targets": "" })"}));

    CAPTURE(test_case);
    auto response_json = nlohmann::json::parse(test_case);
    rc.process_response(std::move(response_json));
    auto next_payload = rc.make_request_payload();

    // no error; targets_version unchanged
    CHECK(next_payload["client"]["state"]["has_error"] == false);
    CHECK(next_payload["client"]["state"]["error"] == "");
    CHECK(next_payload["client"]["state"]["targets_version"] == 0);
  }

  SECTION("ill formatted input",
          "inputs not following the Remote Configuration JSON schema should "
          "generate an error") {
    // clang-format off
    auto test_case = GENERATE(values<std::pair<std::string_view, std::string_view>>({
      // `targets` field is not base64 encoded
      {R"({ "targets": "Hello, Mars!" })",
      "Invalid Remote Configuration response: invalid base64 data for targets"},
      // `targets` field is not a JSON base64 encoded
      // decode("bm90IGpzb24=") == "not json"
      {R"({ "targets": "bm90IGpzb24=" })",
      "Ill-formatted Remote Configuration response: [json.exception.parse_error."
      "101] parse error at line 1, column 2: syntax error while parsing value - invalid literal; last read: 'no'"},
      // `targets` field JSON base64 encoded do not follow the expected schema
      // decode("eyJmb28iOiAiYmFyIn0=") == "{"foo": "bar"}"
      {R"({ "targets": "eyJmb28iOiAiYmFyIn0=" })",
      "Invalid Remote Configuration response: missing signed targets with nonempty \"targets\""},
      // `targets` is missing the `targets` field.
      // decode("eyJzaWduZWQiOiB7InZlcnNpb24iOiAyLCAiY3VzdG9tIjogeyJvcGFxdWVfYmFja2VuZF9zdGF0ZSI6ICIxNSJ9fX0=") == "{"signed": {"version": 2, "custom": {"opaque_backend_state": "15"}}}"
      {R"({
          "targets": "eyJzaWduZWQiOiB7InZlcnNpb24iOiAyLCAiY3VzdG9tIjogeyJvcGFxdWVfYmFja2VuZF9zdGF0ZSI6ICIxNSJ9fX0=",
          "client_configs": ["datadog/2/APM_TRACING/config_id/name"]
      })",
      "JSON error processing key datadog/2/APM_TRACING/config_id/name: [json."
      "exception.out_of_range.403] key 'targets' not found"},
      // `/targets/targets` have no `datadog/APM_TRACING/config_id/name` entry
      // {"signed": {"version": 2, "targets": {"foo": {}, "bar": {}},"custom": {"opaque_backend_state": "15"}}}
      {R"({
          "targets": "eyJzaWduZWQiOiB7InZlcnNpb24iOiAyLCAidGFyZ2V0cyI6IHsiZm9vIjoge30sICJiYXIiOiB7fX0sImN1c3RvbSI6IHsib3BhcXVlX2JhY2tlbmRfc3RhdGUiOiAiMTUifX19",
          "client_configs": ["datadog/2/APM_TRACING/config_id/name"]
      })",
      "Told to apply config for datadog/2/APM_TRACING/config_id/name, but no "
      "corresponding entry exists in targets.targets_signed.targets",
      },
      // `targets` OK but no `target_files` field.
      // {"signed": {"version": 2, "targets": {"datadog/2/APM_TRACING/30/name": {"length": 42, "custom": {"v": 43}, "hashes": {"sha256": ""}}},"custom": {"opaque_backend_state": "15"}}}
      {R"({
          "targets": "eyJzaWduZWQiOiB7InZlcnNpb24iOiAyLCAidGFyZ2V0cyI6IHsiZGF0YWRvZy8yL0FQTV9UUkFDSU5HLzMwL25hbWUiOiB7Imxlbmd0aCI6IDQyLCAiY3VzdG9tIjogeyJ2IjogNDN9LCAiaGFzaGVzIjogeyJzaGEyNTYiOiAiIn19fSwiY3VzdG9tIjogeyJvcGFxdWVfYmFja2VuZF9zdGF0ZSI6ICIxNSJ9fX0K",
          "client_configs": ["datadog/2/APM_TRACING/30/name"]
      })",
      "Told to apply config for datadog/2/APM_TRACING/30/name, but content not present "
      "when it was expected to be (because the new hash differs from the one last "
      "seen, if any)"},
      // `targets` OK. `target_files` field is empty.
      // {"signed": {"version": 2, "targets": {"datadog/2/APM_TRACING/30/name": {"length": 42, "custom": {"v": 43}, "hashes": {"sha256": ""}}},"custom": {"opaque_backend_state": "15"}}}
      {R"({
          "targets": "eyJzaWduZWQiOiB7InZlcnNpb24iOiAyLCAidGFyZ2V0cyI6IHsiZGF0YWRvZy8yL0FQTV9UUkFDSU5HLzMwL25hbWUiOiB7Imxlbmd0aCI6IDQyLCAiY3VzdG9tIjogeyJ2IjogNDN9LCAiaGFzaGVzIjogeyJzaGEyNTYiOiAiIn19fSwiY3VzdG9tIjogeyJvcGFxdWVfYmFja2VuZF9zdGF0ZSI6ICIxNSJ9fX0=",
          "client_configs": ["datadog/2/APM_TRACING/30/name"],
          "target_files": []
      })",
      "Told to apply config for datadog/2/APM_TRACING/30/name, but content not present "
      "when it was expected to be (because the new hash differs from the one last "
      "seen, if any)"},
      // `targets` OK. `target_files` field is not an array.
      // {"signed": {"version": 2, "targets": {"datadog/2/APM_TRACING/30/name": {"length": 42, "custom": {"v": 43}, "hashes": {"sha256": ""}}},"custom": {"opaque_backend_state": "15"}}}
      {R"({
          "targets": "eyJzaWduZWQiOiB7InZlcnNpb24iOiAyLCAidGFyZ2V0cyI6IHsiZGF0YWRvZy8yL0FQTV9UUkFDSU5HLzMwL25hbWUiOiB7Imxlbmd0aCI6IDQyLCAiY3VzdG9tIjogeyJ2IjogNDN9LCAiaGFzaGVzIjogeyJzaGEyNTYiOiAiIn19fSwiY3VzdG9tIjogeyJvcGFxdWVfYmFja2VuZF9zdGF0ZSI6ICIxNSJ9fX0=",
          "client_configs": ["datadog/2/APM_TRACING/30/name"],
          "target_files": 15
      })",
      "Invalid Remote Configuration response: target_files is not an array"},
      // `targets` OK. `target_files` field content is not base64 encoded.
      // {"signed": {"version": 2, "targets": {"datadog/2/APM_TRACING/30/name": {"length": 42, "custom": {"v": 43}, "hashes": {"sha256": ""}}},"custom": {"opaque_backend_state": "15"}}}
      {R"({
          "targets": "eyJzaWduZWQiOiB7InZlcnNpb24iOiAyLCAidGFyZ2V0cyI6IHsiZGF0YWRvZy8yL0FQTV9UUkFDSU5HLzMwL25hbWUiOiB7Imxlbmd0aCI6IDQyLCAiY3VzdG9tIjogeyJ2IjogNDN9LCAiaGFzaGVzIjogeyJzaGEyNTYiOiAiIn19fSwiY3VzdG9tIjogeyJvcGFxdWVfYmFja2VuZF9zdGF0ZSI6ICIxNSJ9fX0=",
          "client_configs": ["datadog/2/APM_TRACING/30/name"],
          "target_files": [{"path": "datadog/2/APM_TRACING/30/name", "raw": "Hello, Uranus!"}]
      })",
      "Invalid Remote Configuration response: target_files[...].raw is not a valid "
      "base64 string"},
      // `targets` has no length provided
      // {"signed": {"version": 2, "targets": {"datadog/2/APM_TRACING/30/name": {"custom": {"v": 43}, "hashes": {"sha256": ""}}},"custom": {"opaque_backend_state": "15"}}}
      {R"({
          "targets": "eyJzaWduZWQiOiB7InZlcnNpb24iOiAyLCAidGFyZ2V0cyI6IHsiZGF0YWRvZy8yL0FQTV9UUkFDSU5HLzMwL25hbWUiOiB7ImN1c3RvbSI6IHsidiI6IDQzfSwgImhhc2hlcyI6IHsic2hhMjU2IjogIiJ9fX0sImN1c3RvbSI6IHsib3BhcXVlX2JhY2tlbmRfc3RhdGUiOiAiMTUifX19",
          "client_configs": ["datadog/2/APM_TRACING/30/name"],
          "target_files": [{"path": "datadog/2/APM_TRACING/30/name", "raw": ""}]
      })",
      "JSON error processing key datadog/2/APM_TRACING/30/name: [json.exception."
      "out_of_range.403] key 'length' not found"},
      // `targets` has non-integer length
      // {"signed": {"version": 2, "targets": {"datadog/2/APM_TRACING/30/name": {"length": "foo", "custom": {"v": 43}, "hashes": {"sha256": ""}}},"custom": {"opaque_backend_state": "15"}}}
      {R"({
          "targets": "eyJzaWduZWQiOiB7InZlcnNpb24iOiAyLCAidGFyZ2V0cyI6IHsiZGF0YWRvZy8yL0FQTV9UUkFDSU5HLzMwL25hbWUiOiB7Imxlbmd0aCI6ICJmb28iLCAiY3VzdG9tIjogeyJ2IjogNDN9LCAiaGFzaGVzIjogeyJzaGEyNTYiOiAiIn19fSwiY3VzdG9tIjogeyJvcGFxdWVfYmFja2VuZF9zdGF0ZSI6ICIxNSJ9fX0=",
          "client_configs": ["datadog/2/APM_TRACING/30/name"],
          "target_files": [{"path": "datadog/2/APM_TRACING/30/name", "raw": ""}]
      })",
      "JSON error processing key datadog/2/APM_TRACING/30/name: [json.exception."
      "type_error.302] type must be number, but is string"},
      // `targets` has no custom field
      // {"signed": {"version": 2, "targets": {"datadog/2/APM_TRACING/30/name": {"length": 2, "hashes": {"sha256": ""}}},"custom": {"opaque_backend_state": "15"}}}
      {R"({
          "targets": "eyJzaWduZWQiOiB7InZlcnNpb24iOiAyLCAidGFyZ2V0cyI6IHsiZGF0YWRvZy8yL0FQTV9UUkFDSU5HLzMwL25hbWUiOiB7Imxlbmd0aCI6IDIsICJoYXNoZXMiOiB7InNoYTI1NiI6ICIifX19LCJjdXN0b20iOiB7Im9wYXF1ZV9iYWNrZW5kX3N0YXRlIjogIjE1In19fQ==",
          "client_configs": ["datadog/2/APM_TRACING/30/name"],
          "target_files": [{"path": "datadog/2/APM_TRACING/30/name", "raw": "YQo="}]
      })",
      "Failed to update config state from for datadog/2/APM_TRACING/30/name: [json."
      "exception.out_of_range.403] key 'custom' not found"},
      // `targets` has an empty "custom"
      // {"signed": {"version": 2, "targets": {"datadog/2/APM_TRACING/30/name": {"length": 2, "custom": {}, "hashes": {"sha256": ""}}},"custom": {"opaque_backend_state": "15"}}}
      {R"({
          "targets": "eyJzaWduZWQiOiB7InZlcnNpb24iOiAyLCAidGFyZ2V0cyI6IHsiZGF0YWRvZy8yL0FQTV9UUkFDSU5HLzMwL25hbWUiOiB7Imxlbmd0aCI6IDIsICJjdXN0b20iOiB7fSwgImhhc2hlcyI6IHsic2hhMjU2IjogIiJ9fX0sImN1c3RvbSI6IHsib3BhcXVlX2JhY2tlbmRfc3RhdGUiOiAiMTUifX19",
          "client_configs": ["datadog/2/APM_TRACING/30/name"],
          "target_files": [{"path": "datadog/2/APM_TRACING/30/name", "raw": "YQo="}]
      })",
      "Failed to update config state from for datadog/2/APM_TRACING/30/name: [json."
      "exception.out_of_range.403] key 'v' not found"},
      // `targets` "custom"/"v" is not a number
      // {"signed": {"version": 2, "targets": {"datadog/2/APM_TRACING/30/name": {"length": 2, "custom": {"v": []}, "hashes": {"sha256": ""}}},"custom": {"opaque_backend_state": "15"}}}
      {R"({
          "targets": "eyJzaWduZWQiOiB7InZlcnNpb24iOiAyLCAidGFyZ2V0cyI6IHsiZGF0YWRvZy8yL0FQTV9UUkFDSU5HLzMwL25hbWUiOiB7Imxlbmd0aCI6IDIsICJjdXN0b20iOiB7InYiOiBbXX0sICJoYXNoZXMiOiB7InNoYTI1NiI6ICIifX19LCJjdXN0b20iOiB7Im9wYXF1ZV9iYWNrZW5kX3N0YXRlIjogIjE1In19fQ==",
          "client_configs": ["datadog/2/APM_TRACING/30/name"],
          "target_files": [{"path": "datadog/2/APM_TRACING/30/name", "raw": "YQo="}]
      })",
      "Failed to update config state from for datadog/2/APM_TRACING/30/name: [json."
      "exception.type_error.302] type must be number, but is array"},
      // `targets` has no "hashes"
      // {"signed": {"version": 2, "targets": {"datadog/2/APM_TRACING/30/name": {"length": 2, "custom": {"v": 1}}},"custom": {"opaque_backend_state": "15"}}}
      {R"({
          "targets": "eyJzaWduZWQiOiB7InZlcnNpb24iOiAyLCAidGFyZ2V0cyI6IHsiZGF0YWRvZy8yL0FQTV9UUkFDSU5HLzMwL25hbWUiOiB7Imxlbmd0aCI6IDIsICJjdXN0b20iOiB7InYiOiAxfX19LCJjdXN0b20iOiB7Im9wYXF1ZV9iYWNrZW5kX3N0YXRlIjogIjE1In19fQ==",
          "client_configs": ["datadog/2/APM_TRACING/30/name"],
          "target_files": [{"path": "datadog/2/APM_TRACING/30/name", "raw": "YQo="}]
      })",
      "Failed to update config state from for datadog/2/APM_TRACING/30/name: [json."
      "exception.out_of_range.403] key 'hashes' not found"},
      // `targets` has "hashes" that's no object
      // {"signed": {"version": 2, "targets": {"datadog/2/APM_TRACING/30/name": {"length": 2, "custom": {"v": 1}, "hashes": []}},"custom": {"opaque_backend_state": "15"}}}
      {R"({
          "targets": "eyJzaWduZWQiOiB7InZlcnNpb24iOiAyLCAidGFyZ2V0cyI6IHsiZGF0YWRvZy8yL0FQTV9UUkFDSU5HLzMwL25hbWUiOiB7Imxlbmd0aCI6IDIsICJjdXN0b20iOiB7InYiOiAxfSwgImhhc2hlcyI6IFtdfX0sImN1c3RvbSI6IHsib3BhcXVlX2JhY2tlbmRfc3RhdGUiOiAiMTUifX19",
          "client_configs": ["datadog/2/APM_TRACING/30/name"],
          "target_files": [{"path": "datadog/2/APM_TRACING/30/name", "raw": "YQo="}]
      })",
      "Failed to update config state from for datadog/2/APM_TRACING/30/name: Invalid "
      "Remote Configuration response in config_target: hashes is not an object"},
      // `targets` has no sha256 hash
      // {"signed": {"version": 2, "targets": {"datadog/2/APM_TRACING/30/name": {"length": 2, "custom": {"v": 1}, "hashes": {}}},"custom": {"opaque_backend_state": "15"}}}
      {R"({
          "targets": "eyJzaWduZWQiOiB7InZlcnNpb24iOiAyLCAidGFyZ2V0cyI6IHsiZGF0YWRvZy8yL0FQTV9UUkFDSU5HLzMwL25hbWUiOiB7Imxlbmd0aCI6IDIsICJjdXN0b20iOiB7InYiOiAxfSwgImhhc2hlcyI6IHt9fX0sImN1c3RvbSI6IHsib3BhcXVlX2JhY2tlbmRfc3RhdGUiOiAiMTUifX19",
          "client_configs": ["datadog/2/APM_TRACING/30/name"],
          "target_files": [{"path": "datadog/2/APM_TRACING/30/name", "raw": "YQo="}]
      })",
      "Failed to update config state from for datadog/2/APM_TRACING/30/name: Invalid "
      "Remote Configuration response in config_target: missing sha256 hash for datadog/"
      "2/APM_TRACING/30/name"},
      // `targets` OK. Length mismatch
      // {"signed": {"version": 2, "targets": {"datadog/2/APM_TRACING/30/name": {"length": 20, "custom": {"v": 1}, "hashes": {"sha256": ""}}},"custom": {"opaque_backend_state": "15"}}}
      {R"({
          "targets": "eyJzaWduZWQiOiB7InZlcnNpb24iOiAyLCAidGFyZ2V0cyI6IHsiZGF0YWRvZy8yL0FQTV9UUkFDSU5HLzMwL25hbWUiOiB7Imxlbmd0aCI6IDIwLCAiY3VzdG9tIjogeyJ2IjogMX0sICJoYXNoZXMiOiB7InNoYTI1NiI6ICIifX19LCJjdXN0b20iOiB7Im9wYXF1ZV9iYWNrZW5kX3N0YXRlIjogIjE1In19fQ==",
          "client_configs": ["datadog/2/APM_TRACING/30/name"],
          "target_files": [{"path": "datadog/2/APM_TRACING/30/name", "raw": "YQo="}]
      })",
      "Invalid Remote Configuration response: target_files[...].raw length (after "
      "decoding) does not match the length in targets.signed.targets. Expected 20, "
      "got 2"},
      // `targets` OK, but product not subscribed
      // {"signed": {"version": 2, "targets": {"datadog/2/ASM_DD/30/name": {"length": 2, "custom": {"v": 1}, "hashes": {"sha256": ""}}},"custom": {"opaque_backend_state": "15"}}}
      {R"({
          "targets": "eyJzaWduZWQiOiB7InZlcnNpb24iOiAyLCAidGFyZ2V0cyI6IHsiZGF0YWRvZy8yL0FTTV9ERC8zMC9uYW1lIjogeyJsZW5ndGgiOiAyLCAiY3VzdG9tIjogeyJ2IjogMX0sICJoYXNoZXMiOiB7InNoYTI1NiI6ICIifX19LCJjdXN0b20iOiB7Im9wYXF1ZV9iYWNrZW5kX3N0YXRlIjogIjE1In19fQo=",
          "client_configs": ["datadog/2/ASM_DD/30/name"],
          "target_files": [{"path": "datadog/2/ASM_DD/30/name", "raw": "YQo="}]
      })",
      "Remote Configuration response contains unknown/unsubscribed product: ASM_DD"},
    }));
    // clang-format on

    CAPTURE(test_case);
    auto response_json = nlohmann::json::parse(/* input = */ test_case.first,
                                               /* parser_callback = */ nullptr,
                                               /* allow_exceptions = */ false);

    REQUIRE(!response_json.is_discarded());
    rc.process_response(std::move(response_json));

    // Next payload should contain an error.
    const auto payload = rc.make_request_payload();
    CHECK(payload["client"]["state"]["has_error"] == true);
    CHECK(payload["client"]["state"]["error"] == test_case.second);
  }

  SECTION("error applying configuration") {
    // clang-format off
    auto test_case = GENERATE(values<std::pair<std::string_view, std::string_view>>({
      // content is not a JSON after base64 decoding
      // decode("bm90IGpzb24=") == "not json"
      // {"signed": {"version": 2, "targets": {"datadog/2/APM_TRACING/30/name": {"length": 8, "custom": {"v": 1}, "hashes": {"sha256": ""}}},"custom": {"opaque_backend_state": "15"}}}
      {R"({
          "targets": "eyJzaWduZWQiOiB7InZlcnNpb24iOiAyLCAidGFyZ2V0cyI6IHsiZGF0YWRvZy8yL0FQTV9UUkFDSU5HLzMwL25hbWUiOiB7Imxlbmd0aCI6IDgsICJjdXN0b20iOiB7InYiOiAxfSwgImhhc2hlcyI6IHsic2hhMjU2IjogIiJ9fX0sImN1c3RvbSI6IHsib3BhcXVlX2JhY2tlbmRfc3RhdGUiOiAiMTUifX19",
          "client_configs": ["datadog/2/APM_TRACING/30/name"],
          "target_files": [{"path": "datadog/2/APM_TRACING/30/name", "raw": "bm90IGpzb24="}]
      })",
      "[json.exception.parse_error.101] parse error at line 1, column 2: syntax "
      "error while parsing value - invalid literal; last read: 'no'"},
      // `targets` OK. `target_files` field JSON base64 content do not follow the expected schema.
      // {"signed": {"version": 2, "targets": {"datadog/2/APM_TRACING/30/name": {"length": 34, "custom": {"v": 1}, "hashes": {"sha256": ""}}, "bar": {}},"custom": {"opaque_backend_state": "15"}}}
      // {"service_target": {"sevice": {}}}
      {R"({
          "targets": "eyJzaWduZWQiOiB7InZlcnNpb24iOiAyLCAidGFyZ2V0cyI6IHsiZGF0YWRvZy8yL0FQTV9UUkFDSU5HLzMwL25hbWUiOiB7Imxlbmd0aCI6IDM0LCAiY3VzdG9tIjogeyJ2IjogMX0sICJoYXNoZXMiOiB7InNoYTI1NiI6ICIifX0sICJiYXIiOiB7fX0sImN1c3RvbSI6IHsib3BhcXVlX2JhY2tlbmRfc3RhdGUiOiAiMTUifX19",
          "client_configs": ["datadog/2/APM_TRACING/30/name"],
          "target_files": [{"path": "datadog/2/APM_TRACING/30/name", "raw": "eyJzZXJ2aWNlX3RhcmdldCI6IHsic2V2aWNlIjoge319fQ=="}]
      })", "[json.exception.out_of_range.403] key 'service' not found"},
    }));
    // clang-format on

    CAPTURE(test_case);
    auto response_json = nlohmann::json::parse(test_case.first);

    const auto config_updated = rc.process_response(std::move(response_json));
    CHECK(config_updated.empty());

    // Next payload should not contain global error.
    const auto payload = rc.make_request_payload();
    CHECK(payload["client"]["state"]["has_error"] == false);
    CHECK(payload["client"]["state"]["error"] == "");

    // However config_states should
    CHECK(payload["client"]["state"]["config_states"].size() == 1);
    CHECK(payload["client"]["state"]["config_states"][0]["id"] == "30");
    CHECK(payload["client"]["state"]["config_states"][0]["version"] == 1);
    CHECK(payload["client"]["state"]["config_states"][0]["product"] ==
          "APM_TRACING");
    CHECK(payload["client"]["state"]["config_states"][0]["apply_state"] ==
          datadog::tracing::remote_config::ConfigState::ApplyState::Error);
    CHECK(payload["client"]["state"]["config_states"][0]["apply_error"] ==
          test_case.second);

    CHECK(payload["client"]["state"]["targets_version"] == 2);
    CHECK(payload["cached_target_files"].size() == 1);
    CHECK(payload["cached_target_files"][0]["hashes"].dump() ==
          R"([{"algorithm":"sha256","hash":""}])");
    CHECK(payload["cached_target_files"][0]["path"] ==
          "datadog/2/APM_TRACING/30/name");
  }

  SECTION("valid remote configuration") {
    // clang-format off
    // {
    //     "lib_config": {
    //         "library_language": "all",
    //         "library_version": "latest",
    //         "service_name": "testsvc",
    //         "env": "test",
    //         "tracing_enabled": false,
    //         "tracing_sampling_rate": 0.6,
    //         "tracing_tags": [
    //             "hello:world",
    //             "foo:bar"
    //         ]
    //     },
    //     "service_target": {
    //         "service": "testsvc",
    //         "env": "test"
    //     }
    // }
    const std::string json_input = R"({
      "targets": "ewogICAgInNpZ25lZCI6IHsKICAgICAgICAiY3VzdG9tIjogewogICAgICAgICAgICAiYWdlbnRfcmVmcmVzaF9pbnRlcnZhbCI6IDUsCiAgICAgICAgICAgICJvcGFxdWVfYmFja2VuZF9zdGF0ZSI6ICJleUoyWlhKemFXOXVJam95TENKemRHRjBaU0k2ZXlKbWFXeGxYMmhoYzJobGN5STZleUprWVhSaFpHOW5MekV3TURBeE1qVTROREF2UVZCTlgxUlNRVU5KVGtjdk9ESTNaV0ZqWmpoa1ltTXpZV0l4TkRNMFpETXlNV05pT0RGa1ptSm1OMkZtWlRZMU5HRTBZall4TVRGalpqRTJOakJpTnpGalkyWTRPVGM0TVRrek9DOHlPVEE0Tm1Ka1ltVTFNRFpsTmpoaU5UQm1NekExTlRneU0yRXpaR0UxWTJVd05USTRaakUyTkRCa05USmpaamc0TmpFNE1UWmhZV0U1Wm1ObFlXWTBJanBiSW05WVpESnBlVU16ZUM5b1JXc3hlWFZoWTFoR04xbHFjWEpwVGs5QldVdHVaekZ0V0UwMU5WWktUSGM5SWwxOWZYMD0iCiAgICAgICAgfSwKICAgICAgICAic3BlY192ZXJzaW9uIjogIjEuMC4wIiwKICAgICAgICAidGFyZ2V0cyI6IHsKICAgICAgICAgICAgImRhdGFkb2cvMi9BUE1fVFJBQ0lORy8zMC9uYW1lIjogewogICAgICAgICAgICAgICAgImhhc2hlcyI6IHsKICAgICAgICAgICAgICAgICAgICAic2hhMjU2IjogImExNzc3NjhiMjBiN2M3Zjg0NDkzNWNhZTY5YzVjNWVkODhlYWFlMjM0ZTAxODJhNzgzNTk5NzMzOWU1NTI0YmMiCiAgICAgICAgICAgICAgICB9LAoJCQkJImN1c3RvbSI6IHsgInYiOiA0MiB9LAogICAgICAgICAgICAgICAgImxlbmd0aCI6IDQyNgogICAgICAgICAgICB9CiAgICAgICAgfSwKICAgICAgICAidmVyc2lvbiI6IDY2MjA0MzIwCiAgICB9Cn0K",
      "client_configs": ["datadog/2/APM_TRACING/30/name"],
      "target_files": [
        {
          "path": "datadog/2/APM_TRACING/30/name",
          "raw": "eyAiaWQiOiAiODI3ZWFjZjhkYmMzYWIxNDM0ZDMyMWNiODFkZmJmN2FmZTY1NGE0YjYxMTFjZjE2NjBiNzFjY2Y4OTc4MTkzOCIsICJyZXZpc2lvbiI6IDE2OTgxNjcxMjYwNjQsICJzY2hlbWFfdmVyc2lvbiI6ICJ2MS4wLjAiLCAiYWN0aW9uIjogImVuYWJsZSIsICJsaWJfY29uZmlnIjogeyAibGlicmFyeV9sYW5ndWFnZSI6ICJhbGwiLCAibGlicmFyeV92ZXJzaW9uIjogImxhdGVzdCIsICJzZXJ2aWNlX25hbWUiOiAidGVzdHN2YyIsICJlbnYiOiAidGVzdCIsICJ0cmFjaW5nX2VuYWJsZWQiOiBmYWxzZSwgInRyYWNpbmdfc2FtcGxpbmdfcmF0ZSI6IDAuNiwgInRyYWNpbmdfdGFncyI6IFsiaGVsbG86d29ybGQiLCAiZm9vOmJhciJdIH0sICJzZXJ2aWNlX3RhcmdldCI6IHsgInNlcnZpY2UiOiAidGVzdHN2YyIsICJlbnYiOiAidGVzdCIgfSB9"
        }
      ]
    })";
    // clang-format on

    auto response_json = nlohmann::json::parse(/* input = */ json_input,
                                               /* parser_callback = */ nullptr,
                                               /* allow_exceptions = */ false);

    REQUIRE(!response_json.is_discarded());

    const auto old_trace_sampler = config_manager->trace_sampler();
    const auto old_span_defaults = config_manager->span_defaults();
    const auto old_report_traces = config_manager->report_traces();
    const auto config_updated = rc.process_response(std::move(response_json));
    REQUIRE(config_updated.size() == 3);
    const auto new_trace_sampler = config_manager->trace_sampler();
    const auto new_span_defaults = config_manager->span_defaults();
    const auto new_report_traces = config_manager->report_traces();

    CHECK(new_trace_sampler != old_trace_sampler);
    CHECK(new_span_defaults != old_span_defaults);
    CHECK(new_report_traces != old_report_traces);

    SECTION("config status is correctly applied") {
      const auto payload = rc.make_request_payload();
      const auto s = payload.dump(2);
      REQUIRE(payload.contains("/client/state/config_states"_json_pointer) ==
              true);

      const auto& config_states =
          payload.at("/client/state/config_states"_json_pointer);
      REQUIRE(config_states.size() == 1);
      CHECK(config_states[0]["product"] == "APM_TRACING");
      CHECK(config_states[0]["apply_state"] == 2);
    }

    SECTION("reset configuration") {
      SECTION(
          "missing from client_configs -> all configurations should be reset") {
        // clang-format off
        // targets.signed.targets == {}
        const std::string json_input = R"({
          "targets": "ewogICAgInNpZ25lZCI6IHsKICAgICAgICAiY3VzdG9tIjogewogICAgICAgICAgICAiYWdlbnRfcmVmcmVzaF9pbnRlcnZhbCI6IDUsCiAgICAgICAgICAgICJvcGFxdWVfYmFja2VuZF9zdGF0ZSI6ICJleUoyWlhKemFXOXVJam95TENKemRHRjBaU0k2ZXlKbWFXeGxYMmhoYzJobGN5STZleUprWVhSaFpHOW5MekV3TURBeE1qVTROREF2UVZCTlgxUlNRVU5KVGtjdk9ESTNaV0ZqWmpoa1ltTXpZV0l4TkRNMFpETXlNV05pT0RGa1ptSm1OMkZtWlRZMU5HRTBZall4TVRGalpqRTJOakJpTnpGalkyWTRPVGM0TVRrek9DOHlPVEE0Tm1Ka1ltVTFNRFpsTmpoaU5UQm1NekExTlRneU0yRXpaR0UxWTJVd05USTRaakUyTkRCa05USmpaamc0TmpFNE1UWmhZV0U1Wm1ObFlXWTBJanBiSW05WVpESnBlVU16ZUM5b1JXc3hlWFZoWTFoR04xbHFjWEpwVGs5QldVdHVaekZ0V0UwMU5WWktUSGM5SWwxOWZYMD0iCiAgICAgICAgfSwKICAgICAgICAic3BlY192ZXJzaW9uIjogIjEuMC4wIiwKICAgICAgICAidGFyZ2V0cyI6IHt9LAogICAgICAgICJ2ZXJzaW9uIjogNjYyMDQzMjAKICAgIH0KfQo=",
          "target_files": []
        })";
        // clang-format on

        auto response_json =
            nlohmann::json::parse(/* input = */ json_input,
                                  /* parser_callback = */ nullptr,
                                  /* allow_exceptions = */ false);

        REQUIRE(!response_json.is_discarded());

        const auto config_updated =
            rc.process_response(std::move(response_json));
        REQUIRE(config_updated.size() == 3);

        const auto current_trace_sampler = config_manager->trace_sampler();
        const auto current_span_defaults = config_manager->span_defaults();
        const auto current_report_traces = config_manager->report_traces();

        CHECK(old_trace_sampler == current_trace_sampler);
        CHECK(old_span_defaults == current_span_defaults);
        CHECK(old_report_traces == current_report_traces);
      }

      SECTION(
          "missing the trace_sampling_rate field -> only this field should be "
          "reset") {
        // clang-format off
        const std::string json_input = R"({
          "targets": "ewogICAgInNpZ25lZCI6IHsKICAgICAgICAiY3VzdG9tIjogewogICAgICAgICAgICAiYWdlbnRfcmVmcmVzaF9pbnRlcnZhbCI6IDUsCiAgICAgICAgICAgICJvcGFxdWVfYmFja2VuZF9zdGF0ZSI6ICJleUoyWlhKemFXOXVJam95TENKemRHRjBaU0k2ZXlKbWFXeGxYMmhoYzJobGN5STZleUprWVhSaFpHOW5MekV3TURBeE1qVTROREF2UVZCTlgxUlNRVU5KVGtjdk9ESTNaV0ZqWmpoa1ltTXpZV0l4TkRNMFpETXlNV05pT0RGa1ptSm1OMkZtWlRZMU5HRTBZall4TVRGalpqRTJOakJpTnpGalkyWTRPVGM0TVRrek9DOHlPVEE0Tm1Ka1ltVTFNRFpsTmpoaU5UQm1NekExTlRneU0yRXpaR0UxWTJVd05USTRaakUyTkRCa05USmpaamc0TmpFNE1UWmhZV0U1Wm1ObFlXWTBJanBiSW05WVpESnBlVU16ZUM5b1JXc3hlWFZoWTFoR04xbHFjWEpwVGs5QldVdHVaekZ0V0UwMU5WWktUSGM5SWwxOWZYMD0iCiAgICAgICAgfSwKICAgICAgICAic3BlY192ZXJzaW9uIjogIjEuMC4wIiwKICAgICAgICAidGFyZ2V0cyI6IHsKICAgICAgICAgICAgImRhdGFkb2cvMi9BUE1fVFJBQ0lORy8zMC9uYW1lIjogewogICAgICAgICAgICAgICAgImhhc2hlcyI6IHsKICAgICAgICAgICAgICAgICAgICAic2hhMjU2IjogIjY5ZTM0NmI1ZmZjZTg0NWUyOTk4NGU3NTliNzFkN2IwN2M1NjE5NzlmYWU5ZTgyZWVkMDgyYzAzOGQ4NmU2YjAiCiAgICAgICAgICAgICAgICB9LAoJCQkJImN1c3RvbSI6IHsgInYiOiA0MiB9LAogICAgICAgICAgICAgICAgImxlbmd0aCI6IDM5NgogICAgICAgICAgICB9CiAgICAgICAgfSwKICAgICAgICAidmVyc2lvbiI6IDY2MjA0MzIwCiAgICB9Cn0K",
          "client_configs": ["datadog/2/APM_TRACING/30/name"],
          "target_files": [
            {
              "path": "datadog/2/APM_TRACING/30/name",
              "raw": "eyAiaWQiOiAiODI3ZWFjZjhkYmMzYWIxNDM0ZDMyMWNiODFkZmJmN2FmZTY1NGE0YjYxMTFjZjE2NjBiNzFjY2Y4OTc4MTkzOCIsICJyZXZpc2lvbiI6IDE2OTgxNjcxMjYwNjQsICJzY2hlbWFfdmVyc2lvbiI6ICJ2MS4wLjAiLCAiYWN0aW9uIjogImVuYWJsZSIsICJsaWJfY29uZmlnIjogeyAibGlicmFyeV9sYW5ndWFnZSI6ICJhbGwiLCAibGlicmFyeV92ZXJzaW9uIjogImxhdGVzdCIsICJzZXJ2aWNlX25hbWUiOiAidGVzdHN2YyIsICJlbnYiOiAidGVzdCIsICJ0cmFjaW5nX2VuYWJsZWQiOiBmYWxzZSwgInRyYWNpbmdfdGFncyI6IFsiaGVsbG86d29ybGQiLCAiZm9vOmJhciJdIH0sICJzZXJ2aWNlX3RhcmdldCI6IHsgInNlcnZpY2UiOiAidGVzdHN2YyIsICJlbnYiOiAidGVzdCIgfSB9"
            }
          ]
        })";
        // clang-format on

        auto response_json =
            nlohmann::json::parse(/* input = */ json_input,
                                  /* parser_callback = */ nullptr,
                                  /* allow_exceptions = */ false);

        REQUIRE(!response_json.is_discarded());

        const auto config_updated =
            rc.process_response(std::move(response_json));
        REQUIRE(config_updated.size() == 1);
        const auto current_trace_sampler = config_manager->trace_sampler();
        CHECK(old_trace_sampler == current_trace_sampler);
      }
    }
  }

  SECTION("update received not for us") {
    // clang-format off
    auto test_case = GENERATE(values<std::string>({
      // "service_target": { "service": "not-testsvc", "env": "test" }
      R"({
        "targets": "ewogICAgInNpZ25lZCI6IHsKICAgICAgICAiY3VzdG9tIjogewogICAgICAgICAgICAiYWdlbnRfcmVmcmVzaF9pbnRlcnZhbCI6IDUsCiAgICAgICAgICAgICJvcGFxdWVfYmFja2VuZF9zdGF0ZSI6ICJleUoyWlhKemFXOXVJam95TENKemRHRjBaU0k2ZXlKbWFXeGxYMmhoYzJobGN5STZleUprWVhSaFpHOW5MekV3TURBeE1qVTROREF2UVZCTlgxUlNRVU5KVGtjdk9ESTNaV0ZqWmpoa1ltTXpZV0l4TkRNMFpETXlNV05pT0RGa1ptSm1OMkZtWlRZMU5HRTBZall4TVRGalpqRTJOakJpTnpGalkyWTRPVGM0TVRrek9DOHlPVEE0Tm1Ka1ltVTFNRFpsTmpoaU5UQm1NekExTlRneU0yRXpaR0UxWTJVd05USTRaakUyTkRCa05USmpaamc0TmpFNE1UWmhZV0U1Wm1ObFlXWTBJanBiSW05WVpESnBlVU16ZUM5b1JXc3hlWFZoWTFoR04xbHFjWEpwVGs5QldVdHVaekZ0V0UwMU5WWktUSGM5SWwxOWZYMD0iCiAgICAgICAgfSwKICAgICAgICAic3BlY192ZXJzaW9uIjogIjEuMC4wIiwKICAgICAgICAidGFyZ2V0cyI6IHsKICAgICAgICAgICAgImRhdGFkb2cvMi9BUE1fVFJBQ0lORy8zMC9uYW1lIjogewogICAgICAgICAgICAgICAgImhhc2hlcyI6IHsKICAgICAgICAgICAgICAgICAgICAic2hhMjU2IjogImExNzc3NjhiMjBiN2M3Zjg0NDkzNWNhZTY5YzVjNWVkODhlYWFlMjM0ZTAxODJhNzgzNTk5NzMzOWU1NTI0YmMiCiAgICAgICAgICAgICAgICB9LAoJCQkJImN1c3RvbSI6IHsgInYiOiA0MiB9LAogICAgICAgICAgICAgICAgImxlbmd0aCI6IDM4NQogICAgICAgICAgICB9CiAgICAgICAgfSwKICAgICAgICAidmVyc2lvbiI6IDY2MjA0MzIwCiAgICB9Cn0K",
        "client_configs": ["datadog/2/APM_TRACING/30/name"],
        "target_files": [
          {
            "path": "datadog/2/APM_TRACING/30/name",
            "raw": "eyAiaWQiOiAiODI3ZWFjZjhkYmMzYWIxNDM0ZDMyMWNiODFkZmJmN2FmZTY1NGE0YjYxMTFjZjE2NjBiNzFjY2Y4OTc4MTkzOCIsICJyZXZpc2lvbiI6IDE2OTgxNjcxMjYwNjQsICJzY2hlbWFfdmVyc2lvbiI6ICJ2MS4wLjAiLCAiYWN0aW9uIjogImVuYWJsZSIsICJsaWJfY29uZmlnIjogeyAibGlicmFyeV9sYW5ndWFnZSI6ICJhbGwiLCAibGlicmFyeV92ZXJzaW9uIjogImxhdGVzdCIsICJzZXJ2aWNlX25hbWUiOiAidGVzdHN2YyIsICJlbnYiOiAidGVzdCIsICJ0cmFjaW5nX2VuYWJsZWQiOiB0cnVlLCAidHJhY2luZ19zYW1wbGluZ19yYXRlIjogMC42IH0sICJzZXJ2aWNlX3RhcmdldCI6IHsgInNlcnZpY2UiOiAibm90LXRlc3RzdmMiLCAiZW52IjogInRlc3QiIH0gfQ=="
          }
        ]
      })",
      // "service_target": { "service": "testsvc", "env": "dev" }
      R"({
        "targets": "ewogICAgInNpZ25lZCI6IHsKICAgICAgICAiY3VzdG9tIjogewogICAgICAgICAgICAiYWdlbnRfcmVmcmVzaF9pbnRlcnZhbCI6IDUsCiAgICAgICAgICAgICJvcGFxdWVfYmFja2VuZF9zdGF0ZSI6ICJleUoyWlhKemFXOXVJam95TENKemRHRjBaU0k2ZXlKbWFXeGxYMmhoYzJobGN5STZleUprWVhSaFpHOW5MekV3TURBeE1qVTROREF2UVZCTlgxUlNRVU5KVGtjdk9ESTNaV0ZqWmpoa1ltTXpZV0l4TkRNMFpETXlNV05pT0RGa1ptSm1OMkZtWlRZMU5HRTBZall4TVRGalpqRTJOakJpTnpGalkyWTRPVGM0TVRrek9DOHlPVEE0Tm1Ka1ltVTFNRFpsTmpoaU5UQm1NekExTlRneU0yRXpaR0UxWTJVd05USTRaakUyTkRCa05USmpaamc0TmpFNE1UWmhZV0U1Wm1ObFlXWTBJanBiSW05WVpESnBlVU16ZUM5b1JXc3hlWFZoWTFoR04xbHFjWEpwVGs5QldVdHVaekZ0V0UwMU5WWktUSGM5SWwxOWZYMD0iCiAgICAgICAgfSwKICAgICAgICAic3BlY192ZXJzaW9uIjogIjEuMC4wIiwKICAgICAgICAidGFyZ2V0cyI6IHsKICAgICAgICAgICAgImRhdGFkb2cvMi9BUE1fVFJBQ0lORy8zMC9uYW1lIjogewogICAgICAgICAgICAgICAgImhhc2hlcyI6IHsKICAgICAgICAgICAgICAgICAgICAic2hhMjU2IjogImExNzc3NjhiMjBiN2M3Zjg0NDkzNWNhZTY5YzVjNWVkODhlYWFlMjM0ZTAxODJhNzgzNTk5NzMzOWU1NTI0YmMiCiAgICAgICAgICAgICAgICB9LAoJCQkJImN1c3RvbSI6IHsgInYiOiA0MiB9LAogICAgICAgICAgICAgICAgImxlbmd0aCI6IDM4MAogICAgICAgICAgICB9CiAgICAgICAgfSwKICAgICAgICAidmVyc2lvbiI6IDY2MjA0MzIwCiAgICB9Cn0K",
        "client_configs": ["datadog/2/APM_TRACING/30/name"],
        "target_files": [
          {
            "path": "datadog/2/APM_TRACING/30/name",
            "raw": "eyAiaWQiOiAiODI3ZWFjZjhkYmMzYWIxNDM0ZDMyMWNiODFkZmJmN2FmZTY1NGE0YjYxMTFjZjE2NjBiNzFjY2Y4OTc4MTkzOCIsICJyZXZpc2lvbiI6IDE2OTgxNjcxMjYwNjQsICJzY2hlbWFfdmVyc2lvbiI6ICJ2MS4wLjAiLCAiYWN0aW9uIjogImVuYWJsZSIsICJsaWJfY29uZmlnIjogeyAibGlicmFyeV9sYW5ndWFnZSI6ICJhbGwiLCAibGlicmFyeV92ZXJzaW9uIjogImxhdGVzdCIsICJzZXJ2aWNlX25hbWUiOiAidGVzdHN2YyIsICJlbnYiOiAidGVzdCIsICJ0cmFjaW5nX2VuYWJsZWQiOiB0cnVlLCAidHJhY2luZ19zYW1wbGluZ19yYXRlIjogMC42IH0sICJzZXJ2aWNlX3RhcmdldCI6IHsgInNlcnZpY2UiOiAidGVzdHN2YyIsICJlbnYiOiAiZGV2IiB9IH0="
          }
        ]
      })"
    }));
    // clang-format on

    CAPTURE(test_case);

    auto response_json = nlohmann::json::parse(/* input = */ test_case,
                                               /* parser_callback = */ nullptr,
                                               /* allow_exceptions = */ false);

    REQUIRE(!response_json.is_discarded());

    const auto old_sampling_rate = config_manager->trace_sampler();
    const auto config_updated = rc.process_response(std::move(response_json));
    const auto new_sampling_rate = config_manager->trace_sampler();

    CHECK(config_updated.empty());
    CHECK(new_sampling_rate == old_sampling_rate);

    auto subseq_payload = rc.make_request_payload();
    CHECK(subseq_payload["client"]["state"]["error"] == "");
    CHECK(subseq_payload["client"]["state"]["config_states"].size() == 1);
    CHECK(subseq_payload["client"]["state"]["config_states"][0]["product"] ==
          "APM_TRACING");
    CHECK(
        subseq_payload["client"]["state"]["config_states"][0]["apply_state"] ==
        datadog::tracing::remote_config::ConfigState::ApplyState::Acknowledged);
  }
}
