// These are tests for `TracerTelemetry`. TracerTelemetry is used to measure
// activity in other parts of the tracer implementation, and construct messages
// that are sent to the datadog agent.

#include <datadog/clock.h>
#include <datadog/span_defaults.h>

#include <datadog/json.hpp>
#include <unordered_set>

#include "datadog/runtime_id.h"
#include "datadog/telemetry/telemetry_impl.h"
#include "mocks/event_schedulers.h"
#include "mocks/http_clients.h"
#include "mocks/loggers.h"
#include "test.h"

using namespace datadog::tracing;
using namespace datadog::telemetry;

namespace {
bool is_valid_telemetry_payload(const nlohmann::json& json) {
  return json.contains("/api_version"_json_pointer) &&
         json.at("api_version") == "v2" &&
         json.contains("/seq_id"_json_pointer) &&
         json.contains("/request_type"_json_pointer) &&
         json.contains("/tracer_time"_json_pointer) &&
         json.contains("/runtime_id"_json_pointer) &&
         json.contains("/payload"_json_pointer) &&
         json.contains("/application"_json_pointer) &&
         json.contains("/host"_json_pointer);
}

}  // namespace

TEST_CASE("Tracer telemetry", "[telemetry]") {
  const std::time_t mock_time = 1672484400;
  const Clock clock = [&mock_time]() {
    TimePoint result;
    result.wall = std::chrono::system_clock::from_time_t(mock_time);
    return result;
  };

  auto logger = std::make_shared<MockLogger>();
  auto client = std::make_shared<MockHTTPClient>();
  auto scheduler = std::make_shared<MockEventScheduler>();

  auto trigger_heartbeat = [&]() {
    // White box testing. The current implementation send a heartbeat every 60s
    // and the task is executed every 10s.
    // TODO(@dmehala): should depends on the config
    scheduler->event_callback();
    scheduler->event_callback();
    scheduler->event_callback();
    scheduler->event_callback();
    scheduler->event_callback();
    scheduler->event_callback();
  };

  const TracerSignature tracer_signature{
      /* runtime_id = */ RuntimeID::generate(),
      /* service = */ "testsvc",
      /* environment = */ "test"};

  const std::string ignore{""};

  auto url = HTTPClient::URL::parse("http://localhost:8000");
  Telemetry telemetry{*finalize_config(),
                      logger,
                      client,
                      std::vector<std::shared_ptr<Metric>>{},
                      scheduler,
                      *url,
                      clock};

  SECTION("generates app-started message") {
    SECTION("Without a defined integration") {
      /// By default the integration is `datadog` with the tracer version.
      /// TODO: remove the default because these datadog are already part of the
      /// request header.
      telemetry.send_app_started({});

      auto app_started = nlohmann::json::parse(client->request_body);
      REQUIRE(is_valid_telemetry_payload(app_started) == true);
      REQUIRE(app_started["request_type"] == "message-batch");
      REQUIRE(app_started["payload"].size() == 2);

      auto& app_started_payload = app_started["payload"][0];
      CHECK(app_started_payload["request_type"] == "app-started");
      CHECK(app_started_payload["payload"]["configuration"].empty());
    }

    SECTION("With an integration") {
      Configuration cfg;
      cfg.integration_name = "nginx";
      cfg.integration_version = "1.25.2";
      Telemetry telemetry2{*finalize_config(cfg),
                           logger,
                           client,
                           std::vector<std::shared_ptr<Metric>>{},
                           scheduler,
                           *url};

      client->clear();
      telemetry2.send_app_started({});

      auto app_started = nlohmann::json::parse(client->request_body);
      REQUIRE(is_valid_telemetry_payload(app_started) == true);
      REQUIRE(app_started["request_type"] == "message-batch");
      REQUIRE(app_started["payload"].size() == 2);

      const std::unordered_set<std::string> expected{"app-started",
                                                     "app-integrations-change"};

      for (const auto& payload : app_started["payload"]) {
        CHECK(expected.find(payload["request_type"]) != expected.cend());
      }
    }

    SECTION("With configuration") {
      std::unordered_map<ConfigName, ConfigMetadata> configuration{
          {ConfigName::SERVICE_NAME,
           ConfigMetadata(ConfigName::SERVICE_NAME, "foo",
                          ConfigMetadata::Origin::CODE)}};

      client->clear();
      telemetry.send_app_started(configuration);

      auto app_started = nlohmann::json::parse(client->request_body);
      REQUIRE(is_valid_telemetry_payload(app_started) == true);
      REQUIRE(app_started["request_type"] == "message-batch");
      REQUIRE(app_started["payload"].is_array());
      REQUIRE(app_started["payload"].size() == 2);

      auto& app_started_payload = app_started["payload"][0];
      CHECK(app_started_payload["request_type"] == "app-started");

      auto cfg_payload = app_started_payload["payload"]["configuration"];
      REQUIRE(cfg_payload.is_array());
      REQUIRE(cfg_payload.size() == 1);

      // clang-format off
      const auto expected_conf = nlohmann::json({
        {"name", "service"},
        {"value", "foo"},
        {"seq_id", 1},
        {"origin", "code"},
      });
      // clang-format on

      CHECK(cfg_payload[0] == expected_conf);

      SECTION("generates a configuration change event") {
        SECTION("empty configuration do not generate a valid payload") {
          client->clear();
          telemetry.send_configuration_change();

          CHECK(client->request_body.empty());
        }

        SECTION("valid configurations update") {
          const std::vector<ConfigMetadata> new_config{
              {ConfigName::SERVICE_NAME, "increase seq_id",
               ConfigMetadata::Origin::ENVIRONMENT_VARIABLE},
              {ConfigName::REPORT_TRACES, "", ConfigMetadata::Origin::DEFAULT,
               Error{Error::Code::OTHER, "empty field"}}};

          client->clear();
          telemetry.capture_configuration_change(new_config);
          telemetry.send_configuration_change();

          auto updates = client->request_body;
          REQUIRE(!updates.empty());
          auto config_change_message =
              nlohmann::json::parse(updates, nullptr, false);
          REQUIRE(config_change_message.is_discarded() == false);
          REQUIRE(is_valid_telemetry_payload(config_change_message) == true);

          CHECK(config_change_message["request_type"] ==
                "app-client-configuration-change");
          CHECK(config_change_message["payload"]["configuration"].is_array());
          CHECK(config_change_message["payload"]["configuration"].size() == 2);

          const std::unordered_map<std::string, nlohmann::json> expected_json{
              {
                  "service",
                  nlohmann::json{
                      {"name", "service"},
                      {"value", "increase seq_id"},
                      {"seq_id", 2},
                      {"origin", "env_var"},
                  },
              },
              {
                  "trace_enabled",
                  nlohmann::json{
                      {"name", "trace_enabled"},
                      {"value", ""},
                      {"seq_id", 1},
                      {"origin", "default"},
                      {
                          "error",
                          {
                              {"code", Error::Code::OTHER},
                              {"message", "empty field"},
                          },
                      },
                  },
              },
          };

          for (const auto& conf :
               config_change_message["payload"]["configuration"]) {
            auto it = expected_json.find(conf["name"]);
            REQUIRE(it != expected_json.cend());
            CHECK(it->second == conf);
          }

          // No update -> no configuration update
          client->clear();
          telemetry.send_configuration_change();
          CHECK(client->request_body.empty());
        }
      }
    }
  }

  SECTION("generates a heartbeat message") {
    client->clear();
    trigger_heartbeat();

    auto heartbeat_message = client->request_body;
    auto message_batch = nlohmann::json::parse(heartbeat_message);
    REQUIRE(is_valid_telemetry_payload(message_batch) == true);
    REQUIRE(message_batch["payload"].size() == 1);
    auto heartbeat = message_batch["payload"][0];
    REQUIRE(heartbeat["request_type"] == "app-heartbeat");
  }

  SECTION("captures metrics and sends generate-metrics payload") {
    telemetry.metrics().tracer.trace_segments_created_new.inc();
    REQUIRE(telemetry.metrics().tracer.trace_segments_created_new.value() == 1);
    trigger_heartbeat();

    REQUIRE(telemetry.metrics().tracer.trace_segments_created_new.value() == 0);

    auto heartbeat_and_telemetry_message = client->request_body;
    auto message_batch = nlohmann::json::parse(heartbeat_and_telemetry_message);
    REQUIRE(is_valid_telemetry_payload(message_batch) == true);
    REQUIRE(message_batch["payload"].size() == 2);
    auto generate_metrics = message_batch["payload"][1];
    REQUIRE(generate_metrics["request_type"] == "generate-metrics");
    auto payload = generate_metrics["payload"];
    auto series = payload["series"];
    REQUIRE(series.size() == 1);
    auto metric = series[0];
    REQUIRE(metric["metric"] == "trace_segments_created");
    auto tags = metric["tags"];
    REQUIRE(tags.size() == 1);
    REQUIRE(tags[0] == "new_continued:new");
    auto points = metric["points"];
    REQUIRE(points.size() == 1);
    REQUIRE(points[0][0] == mock_time);
    REQUIRE(points[0][1] == 1);
  }

  SECTION("generates an app-closing event") {
    client->clear();
    telemetry.send_app_closing();

    auto message_batch = nlohmann::json::parse(client->request_body);
    REQUIRE(is_valid_telemetry_payload(message_batch) == true);
    REQUIRE(message_batch["payload"].size() == 1);
    auto heartbeat = message_batch["payload"][0];
    REQUIRE(heartbeat["request_type"] == "app-closing");
  }

  SECTION("logs serialization") {
    SECTION("log level is correct") {
      struct TestCase {
        std::string_view name;
        std::string input;
        Optional<std::string> stacktrace;
        std::function<void(Telemetry&, const std::string&,
                           const Optional<std::string>& stacktrace)>
            apply;
        std::string expected_log_level;
      };

      auto test_case = GENERATE(values<TestCase>({
          {
              "warning log",
              "This is a warning log!",
              nullopt,
              [](Telemetry& telemetry, const std::string& input,
                 const Optional<std::string>&) {
                telemetry.log_warning(input);
              },
              "WARNING",
          },
          {
              "error log",
              "This is an error log!",
              nullopt,
              [](Telemetry& telemetry, const std::string& input,
                 const Optional<std::string>&) { telemetry.log_error(input); },
              "ERROR",
          },
          {
              "error log with stacktrace",
              "This is an error log with a fake stacktrace!",
              "error here\nthen here\nfinally here\n",
              [](Telemetry& telemetry, const std::string& input,
                 Optional<std::string> stacktrace) {
                telemetry.log_error(input, *stacktrace);
              },
              "ERROR",
          },
      }));

      CAPTURE(test_case.name);

      client->clear();
      test_case.apply(telemetry, test_case.input, test_case.stacktrace);
      trigger_heartbeat();

      auto message_batch = nlohmann::json::parse(client->request_body);
      REQUIRE(is_valid_telemetry_payload(message_batch));
      REQUIRE(message_batch["payload"].size() == 2);

      auto logs_message = message_batch["payload"][1];
      REQUIRE(logs_message["request_type"] == "logs");

      auto logs_payload = logs_message["payload"]["logs"];
      REQUIRE(logs_payload.size() == 1);
      CHECK(logs_payload[0]["level"] == test_case.expected_log_level);
      CHECK(logs_payload[0]["message"] == test_case.input);
      CHECK(logs_payload[0].contains("tracer_time"));

      if (test_case.stacktrace) {
        CHECK(logs_payload[0]["stack_trace"] == test_case.stacktrace);
      } else {
        CHECK(logs_payload[0].contains("stack_trace") == false);
      }
    }
  }
}
