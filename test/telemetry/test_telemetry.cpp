// These are tests for `TracerTelemetry`. TracerTelemetry is used to measure
// activity in other parts of the tracer implementation, and construct messages
// that are sent to the datadog agent.

#include <datadog/clock.h>
#include <datadog/span_defaults.h>

#include <datadog/json.hpp>
#include <unordered_set>

#include "datadog/runtime_id.h"
#include "datadog/telemetry/telemetry_impl.h"
#include "mocks/http_clients.h"
#include "mocks/loggers.h"
#include "test.h"

using namespace datadog::tracing;
using namespace datadog::telemetry;
using namespace std::chrono_literals;

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

struct FakeEventScheduler : public EventScheduler {
  size_t count_tasks = 0;
  std::function<void()> heartbeat_callback = nullptr;
  std::function<void()> metrics_callback = nullptr;
  Optional<std::chrono::steady_clock::duration> heartbeat_interval;
  Optional<std::chrono::steady_clock::duration> metrics_interval;
  bool cancelled = false;

  // NOTE: White box testing. This is a limitation of the event scheduler API.
  Cancel schedule_recurring_event(std::chrono::steady_clock::duration interval,
                                  std::function<void()> callback) override {
    if (count_tasks == 0) {
      heartbeat_callback = callback;
      heartbeat_interval = interval;
    } else if (count_tasks == 1) {
      metrics_callback = callback;
      metrics_interval = interval;
    }
    count_tasks++;
    return [this]() { cancelled = true; };
  }

  void trigger_heartbeat() {
    assert(heartbeat_callback != nullptr);
    heartbeat_callback();
  }

  void trigger_metrics_capture() {
    assert(metrics_callback != nullptr);
    metrics_callback();
  }

  std::string config() const override {
    return nlohmann::json::object({{"type", "FakeEventScheduler"}}).dump();
  }
};

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
  auto scheduler = std::make_shared<FakeEventScheduler>();

  const TracerSignature tracer_signature{
      /* runtime_id = */ RuntimeID::generate(),
      /* service = */ "testsvc",
      /* environment = */ "test"};

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
    scheduler->trigger_heartbeat();

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
    scheduler->trigger_metrics_capture();
    scheduler->trigger_heartbeat();

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
      scheduler->trigger_heartbeat();

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

TEST_CASE("Tracer telemetry configuration", "[telemetry]") {
  // Cases:
  //  - when `report_metrics` is set to false. No metrics are reported.
  //  - when `report_logs` is set to false. No logs are reported.
  //  - respects interval defined.
  //  - telemetry disabled doesn't send anything.

  auto logger = std::make_shared<MockLogger>();
  auto client = std::make_shared<MockHTTPClient>();
  auto scheduler = std::make_shared<FakeEventScehduler>();
  std::vector<std::shared_ptr<Metric>> metrics;

  const TracerSignature tracer_signature{
      /* runtime_id = */ RuntimeID::generate(),
      /* service = */ "testsvc",
      /* environment = */ "test"};

  auto url = HTTPClient::URL::parse("http://localhost:8000");

  SECTION("disabling metrics reporting do not collect metrics") {
    Configuration cfg;
    cfg.report_metrics = false;

    auto final_cfg = finalize_config(cfg);
    REQUIRE(final_cfg);

    Telemetry telemetry(*final_cfg, logger, client, metrics, scheduler, *url);
    CHECK(scheduler->metrics_callback == nullptr);
    CHECK(scheduler->metrics_interval == nullopt);
  }

  SECTION("intervals are respected") {
    Configuration cfg;
    cfg.metrics_interval_seconds = .5;
    cfg.heartbeat_interval_seconds = 30;

    auto final_cfg = finalize_config(cfg);
    REQUIRE(final_cfg);

    Telemetry telemetry(*final_cfg, logger, client, metrics, scheduler, *url);
    CHECK(scheduler->metrics_callback != nullptr);
    CHECK(scheduler->metrics_interval == 500ms);

    CHECK(scheduler->heartbeat_callback != nullptr);
    CHECK(scheduler->metrics_interval != 30s);
  }

  SECTION("disabling logs reporting do not collect logs") {
    client->clear();

    Configuration cfg;
    cfg.report_logs = false;

    auto final_cfg = finalize_config(cfg);
    REQUIRE(final_cfg);

    Telemetry telemetry(*final_cfg, logger, client, metrics, scheduler, *url);
    telemetry.log_error("error");

    // NOTE(@dmehala): logs are sent with an heartbeat.
    scheduler->trigger_heartbeat();

    auto message_batch = nlohmann::json::parse(client->request_body);
    REQUIRE(is_valid_telemetry_payload(message_batch));
    REQUIRE(message_batch["payload"].size() == 1);
    CHECK(message_batch["payload"][0]["request_type"] == "app-heartbeat");
  }
}
