// These are tests for `TracerTelemetry`. TracerTelemetry is used to measure
// activity in other parts of the tracer implementation, and construct messages
// that are sent to the datadog agent.

#include <datadog/clock.h>
#include <datadog/span_defaults.h>

#include <datadog/json.hpp>
#include <unordered_set>

#include "../common/environment.h"
#include "datadog/base64.h"
#include "datadog/runtime_id.h"
#include "datadog/telemetry/log_binned_distr.h"
#include "datadog/telemetry/telemetry_impl.h"
#include "mocks/http_clients.h"
#include "mocks/loggers.h"
#include "test.h"

namespace ddtest = datadog::test;
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

std::optional<nlohmann::json> find_payload(const nlohmann::json& messages,
                                           std::string_view kind) {
  for (const auto& m : messages) {
    if (m["request_type"].get<std::string_view>() == kind) return m;
  }

  return std::nullopt;
};

struct ErrorTags {
  bool error = false;

  static auto tag_fields() {
    return std::tuple{tag("error", &ErrorTags::error)};
  }
};

std::string sketch_b64(std::initializer_list<double> values) {
  LogBinnedDistr sketch;
  for (double value : values) sketch.record(value);
  return base64_encode(sketch.encode_protobuf());
}

// The series of `payload` in the namespace `scope`.
nlohmann::json series_in(const nlohmann::json& payload,
                         std::string_view scope) {
  auto series = nlohmann::json::array();
  for (const auto& one_series : payload["payload"]["series"]) {
    if (one_series["namespace"] == scope) series.push_back(one_series);
  }
  return series;
}

struct FakeEventScheduler : public EventScheduler {
  size_t count_tasks = 0;
  std::function<void()> heartbeat_callback = nullptr;
  std::function<void()> metrics_callback = nullptr;
  std::function<void()> extended_heartbeat_callback = nullptr;
  Optional<std::chrono::steady_clock::duration> heartbeat_interval;
  Optional<std::chrono::steady_clock::duration> metrics_interval;
  Optional<std::chrono::steady_clock::duration> extended_heartbeat_interval;
  bool cancelled = false;

  // NOTE: White box testing. This is a limitation of the event scheduler API.
  // Tasks are registered in order: heartbeat (0), metrics (1, if enabled),
  // extended heartbeat (last).
  Cancel schedule_recurring_event(std::chrono::steady_clock::duration interval,
                                  std::function<void()> callback) override {
    if (count_tasks == 0) {
      heartbeat_callback = callback;
      heartbeat_interval = interval;
    } else if (interval <= std::chrono::minutes(1)) {
      // Metrics interval is <= 60s; extended heartbeat is much larger.
      metrics_callback = callback;
      metrics_interval = interval;
    } else {
      extended_heartbeat_callback = callback;
      extended_heartbeat_interval = interval;
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

  void trigger_extended_heartbeat() {
    assert(extended_heartbeat_callback != nullptr);
    extended_heartbeat_callback();
  }

  std::string config() const override {
    return nlohmann::json::object({{"type", "FakeEventScheduler"}}).dump();
  }
};

}  // namespace

#define TELEMETRY_IMPLEMENTATION_TEST(x) \
  TEST_CASE(x, "[telemetry],[telemetry.impl]")

TELEMETRY_IMPLEMENTATION_TEST("Tracer telemetry lifecycle") {
  auto logger = std::make_shared<MockLogger>();
  auto client = std::make_shared<MockHTTPClient>();
  auto scheduler = std::make_shared<FakeEventScheduler>();

  const TracerSignature tracer_signature{
      /* runtime_id = */ RuntimeID::generate(),
      /* service = */ "testsvc",
      /* environment = */ "test"};

  auto url = HTTPClient::URL::parse("http://localhost:8000");

  SECTION("ctor send app-started message") {
    SECTION("Without a defined integration") {
      auto telemetry = Telemetry::create(*finalize_config(), tracer_signature,
                                         logger, client, scheduler, *url);
      /// By default the integration is `datadog` with the tracer version.
      /// TODO: remove the default because these datadog are already part of the
      /// request header.
      auto app_started = nlohmann::json::parse(client->request_body);
      REQUIRE(is_valid_telemetry_payload(app_started) == true);
      REQUIRE(app_started["request_type"] == "message-batch");
      REQUIRE(app_started["payload"].size() == 2);

      auto& app_started_payload = app_started["payload"][0];
      CHECK(app_started_payload["request_type"] == "app-started");
      CHECK(app_started_payload["payload"]["configuration"].empty());
    }

    SECTION("With an integration") {
      client->clear();

      Configuration cfg;
      cfg.integration_name = "nginx";
      cfg.integration_version = "1.25.2";
      auto telemetry2 =
          Telemetry::create(*finalize_config(cfg), tracer_signature, logger,
                            client, scheduler, *url);

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

    SECTION("With installation signature") {
      client->clear();

      ddtest::EnvGuard install_id_env("DD_INSTRUMENTATION_INSTALL_ID",
                                      "68e75c48-57ca-4a12-adfc-575c4b05fcbe");
      ddtest::EnvGuard install_type_env("DD_INSTRUMENTATION_INSTALL_TYPE",
                                        "k8s_single_step");
      ddtest::EnvGuard install_time_env("DD_INSTRUMENTATION_INSTALL_TIME",
                                        "1703188212");

      auto telemetry4 = Telemetry::create(*finalize_config(), tracer_signature,
                                          logger, client, scheduler, *url);

      auto app_started = nlohmann::json::parse(client->request_body);
      REQUIRE(is_valid_telemetry_payload(app_started) == true);
      REQUIRE(app_started["request_type"] == "message-batch");
      REQUIRE(app_started["payload"].is_array());
      REQUIRE(app_started["payload"].size() == 2);

      auto& app_started_payload = app_started["payload"][0];
      CHECK(app_started_payload["request_type"] == "app-started");

      auto install_payload =
          app_started_payload["payload"]["install_signature"];
      REQUIRE(install_payload.is_object());

      REQUIRE(install_payload.contains("install_id") == true);
      CHECK(install_payload["install_id"] ==
            "68e75c48-57ca-4a12-adfc-575c4b05fcbe");

      REQUIRE(install_payload.contains("install_id") == true);
      CHECK(install_payload["install_type"] == "k8s_single_step");

      REQUIRE(install_payload.contains("install_id") == true);
      CHECK(install_payload["install_time"] == "1703188212");
    }

    SECTION("With configuration") {
      client->clear();

      Product product;
      product.name = Product::Name::tracing;
      product.enabled = true;
      product.version = tracer_version;
      product.configurations =
          std::unordered_map<ConfigName, std::vector<ConfigMetadata>>{
              {ConfigName::SERVICE_NAME,
               {ConfigMetadata(ConfigName::SERVICE_NAME, "default-service",
                               ConfigMetadata::Origin::DEFAULT),
                ConfigMetadata(ConfigName::SERVICE_NAME, "code-service",
                               ConfigMetadata::Origin::CODE),
                ConfigMetadata(ConfigName::SERVICE_NAME, "env-service",
                               ConfigMetadata::Origin::ENVIRONMENT_VARIABLE)}},
          };

      Configuration cfg;
      cfg.products.emplace_back(std::move(product));

      auto telemetry3 =
          Telemetry::create(*finalize_config(cfg), tracer_signature, logger,
                            client, scheduler, *url);

      auto app_started = nlohmann::json::parse(client->request_body);
      REQUIRE(is_valid_telemetry_payload(app_started) == true);
      REQUIRE(app_started["request_type"] == "message-batch");
      REQUIRE(app_started["payload"].is_array());
      REQUIRE(app_started["payload"].size() == 2);

      auto& app_started_payload = app_started["payload"][0];
      CHECK(app_started_payload["request_type"] == "app-started");

      auto cfg_payload = app_started_payload["payload"]["configuration"];
      REQUIRE(cfg_payload.is_array());
      REQUIRE(cfg_payload.size() == 3);

      // Each configuration source is sent with an incrementing seq_id
      // clang-format off
      const std::unordered_map<std::string, nlohmann::json> expected_configs{
        {"default-service", nlohmann::json({
          {"name", "service"},
          {"value", "default-service"},
          {"seq_id", 1},
          {"origin", "default"},
        })},
        {"code-service", nlohmann::json({
          {"name", "service"},
          {"value", "code-service"},
          {"seq_id", 2},
          {"origin", "code"},
        })},
        {"env-service", nlohmann::json({
          {"name", "service"},
          {"value", "env-service"},
          {"seq_id", 3},
          {"origin", "env_var"},
        })},
      };
      // clang-format on

      // Verify all three configuration sources are present
      for (const auto& conf : cfg_payload) {
        auto value = conf["value"].get<std::string>();
        REQUIRE(expected_configs.count(value) == 1);
        CHECK(conf == expected_configs.at(value));
      }

      SECTION("generates a configuration change event") {
        SECTION("empty configuration do not generate a valid payload") {
          client->clear();
          telemetry3->send_configuration_change();

          CHECK(client->request_body.empty());
        }

        SECTION("valid configurations update") {
          const std::vector<ConfigMetadata> new_config{
              {ConfigName::SERVICE_NAME, "increase seq_id",
               ConfigMetadata::Origin::ENVIRONMENT_VARIABLE},
              {ConfigName::REPORT_TRACES, "", ConfigMetadata::Origin::DEFAULT,
               Error{Error::Code::OTHER, "empty field"}}};

          client->clear();
          telemetry3->capture_configuration_change(new_config);
          telemetry3->send_configuration_change();

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
                      {"seq_id",
                       4},  // seq_id 4 (after DEFAULT=1, CODE=2, ENV=3)
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
          telemetry3->send_configuration_change();
          CHECK(client->request_body.empty());
        }
      }
    }

    SECTION("Product error is nested under the product name") {
      client->clear();

      Configuration cfg;
      cfg.products.emplace_back(Product{Product::Name::appsec,
                                        false,
                                        "1.2.3",
                                        12,
                                        "Error initializing WAF",
                                        {}});

      auto telemetry =
          Telemetry::create(*finalize_config(cfg), tracer_signature, logger,
                            client, scheduler, *url);

      const auto message_batch = nlohmann::json::parse(client->request_body);
      const auto& appsec = message_batch.at("payload")
                               .at(0)
                               .at("payload")
                               .at("products")
                               .at("appsec");

      CHECK(appsec ==
            nlohmann::json{
                {"version", "1.2.3"},
                {"enabled", false},
                {"error",
                 {{"code", 12}, {"message", "Error initializing WAF"}}}});
    }
  }

  SECTION("dtor send app-closing message") {
    {
      auto telemetry = Telemetry::create(*finalize_config(), tracer_signature,
                                         logger, client, scheduler, *url);
      client->clear();
    }

    auto message_batch = nlohmann::json::parse(client->request_body);
    REQUIRE(is_valid_telemetry_payload(message_batch) == true);
    REQUIRE(message_batch["payload"].size() >= 1);

    auto app_closing_payload =
        find_payload(message_batch["payload"], "app-closing");
    REQUIRE(app_closing_payload.has_value());
  }
}

TELEMETRY_IMPLEMENTATION_TEST("session ID headers") {
  auto logger = std::make_shared<MockLogger>();
  auto client = std::make_shared<MockHTTPClient>();
  auto scheduler = std::make_shared<FakeEventScheduler>();
  auto url = HTTPClient::URL::parse("http://localhost:8000");

  SECTION("root process: DD-Session-ID present, DD-Root-Session-ID absent") {
    auto session_rid = RuntimeID::generate();
    const TracerSignature tracer_signature(session_rid, "testsvc", "test");

    auto telemetry = Telemetry::create(*finalize_config(), tracer_signature,
                                       logger, client, scheduler, *url);

    auto it = client->request_headers.items.find("DD-Session-ID");
    REQUIRE(it != client->request_headers.items.end());
    CHECK(it->second == session_rid.string());

    CHECK(client->request_headers.items.find("DD-Root-Session-ID") ==
          client->request_headers.items.end());
  }

  SECTION("child process: DD-Root-Session-ID present when different") {
    auto session_rid = RuntimeID::generate();
    auto root_rid = RuntimeID::generate();
    const TracerSignature tracer_signature(session_rid, root_rid.string(),
                                           "testsvc", "test");

    auto telemetry = Telemetry::create(*finalize_config(), tracer_signature,
                                       logger, client, scheduler, *url);

    auto session_it = client->request_headers.items.find("DD-Session-ID");
    REQUIRE(session_it != client->request_headers.items.end());
    CHECK(session_it->second == session_rid.string());

    auto root_it = client->request_headers.items.find("DD-Root-Session-ID");
    REQUIRE(root_it != client->request_headers.items.end());
    CHECK(root_it->second == root_rid.string());
  }

  SECTION("heartbeat includes session headers") {
    auto session_rid = RuntimeID::generate();
    auto root_rid = RuntimeID::generate();
    const TracerSignature tracer_signature(session_rid, root_rid.string(),
                                           "testsvc", "test");

    auto telemetry = Telemetry::create(*finalize_config(), tracer_signature,
                                       logger, client, scheduler, *url);

    client->clear();
    scheduler->trigger_heartbeat();

    auto session_it = client->request_headers.items.find("DD-Session-ID");
    REQUIRE(session_it != client->request_headers.items.end());
    CHECK(session_it->second == session_rid.string());

    auto root_it = client->request_headers.items.find("DD-Root-Session-ID");
    REQUIRE(root_it != client->request_headers.items.end());
    CHECK(root_it->second == root_rid.string());
  }
}

TELEMETRY_IMPLEMENTATION_TEST("Tracer telemetry API") {
  const Clock clock = [] {
    TimePoint result;
    result.wall = std::chrono::system_clock::from_time_t(1672484400);
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

  auto telemetry = Telemetry::create(*finalize_config(), tracer_signature,
                                     logger, client, scheduler, *url, clock);

  SECTION("generates a heartbeat message") {
    client->clear();
    scheduler->trigger_heartbeat();

    auto heartbeat_message = client->request_body;
    auto message_batch = nlohmann::json::parse(heartbeat_message);
    REQUIRE(is_valid_telemetry_payload(message_batch) == true);
    REQUIRE(message_batch["payload"].size() >= 1);

    REQUIRE(find_payload(message_batch["payload"], "app-heartbeat"));
    CHECK(!find_payload(message_batch["payload"], "app-product-change"));
  }

  SECTION("heartbeat sends the latest product changes once") {
    telemetry->capture_product_change(
        Product{Product::Name::appsec, true, "1.0.0", {}, {}, {}});
    telemetry->capture_product_change(Product{
        Product::Name::appsec, false, "1.0.1", 3, "WAF init failed", {}});
    telemetry->capture_product_change(
        Product{Product::Name::profiler, true, "2.0.0", {}, {}, {}});
    telemetry->capture_product_change(
        Product{Product::Name::tracing, true, "3.0.0", {}, {}, {}});

    client->clear();
    scheduler->trigger_heartbeat();

    auto message_batch = nlohmann::json::parse(client->request_body);
    REQUIRE(is_valid_telemetry_payload(message_batch));
    auto product_change =
        find_payload(message_batch["payload"], "app-product-change");
    REQUIRE(product_change);
    CHECK((*product_change)["payload"] ==
          nlohmann::json{
              {"products",
               {
                   {"appsec",
                    {{"version", "1.0.1"},
                     {"enabled", false},
                     {"error", {{"code", 3}, {"message", "WAF init failed"}}}}},
                   {"profiler", {{"version", "2.0.0"}, {"enabled", true}}},
               }},
          });

    client->clear();
    scheduler->trigger_heartbeat();

    message_batch = nlohmann::json::parse(client->request_body);
    CHECK(!find_payload(message_batch["payload"], "app-product-change"));
  }

  SECTION("tracing product change alone is not sent") {
    telemetry->capture_product_change(
        Product{Product::Name::tracing, false, "3.0.0", {}, {}, {}});

    client->clear();
    scheduler->trigger_heartbeat();

    auto message_batch = nlohmann::json::parse(client->request_body);
    CHECK(!find_payload(message_batch["payload"], "app-product-change"));
  }

  SECTION("generates an extended heartbeat with configuration") {
    client->clear();

    Product product;
    product.name = Product::Name::tracing;
    product.enabled = true;
    product.version = tracer_version;
    product.configurations =
        std::unordered_map<ConfigName, std::vector<ConfigMetadata>>{
            {ConfigName::SERVICE_NAME,
             {ConfigMetadata(ConfigName::SERVICE_NAME, "my-service",
                             ConfigMetadata::Origin::CODE)}},
        };

    Configuration cfg;
    cfg.products.emplace_back(std::move(product));

    auto scheduler2 = std::make_shared<FakeEventScheduler>();
    auto telemetry2 = Telemetry::create(*finalize_config(cfg), tracer_signature,
                                        logger, client, scheduler2, *url);

    client->clear();
    scheduler2->trigger_extended_heartbeat();

    auto message_batch = nlohmann::json::parse(client->request_body);
    REQUIRE(is_valid_telemetry_payload(message_batch));

    auto ext_hb =
        find_payload(message_batch["payload"], "app-extended-heartbeat");
    REQUIRE(ext_hb.has_value());

    auto& config_payload = (*ext_hb)["payload"]["configuration"];
    REQUIRE(config_payload.is_array());
    REQUIRE(config_payload.size() == 1);
    CHECK(config_payload[0]["name"] == "service");
    CHECK(config_payload[0]["value"] == "my-service");
    CHECK(config_payload[0]["origin"] == "code");
  }

  SECTION(
      "extended heartbeat reflects runtime configuration changes (remote "
      "config)") {
    client->clear();

    Product product;
    product.name = Product::Name::tracing;
    product.enabled = true;
    product.version = tracer_version;
    product.configurations =
        std::unordered_map<ConfigName, std::vector<ConfigMetadata>>{
            {ConfigName::SERVICE_NAME,
             {ConfigMetadata(ConfigName::SERVICE_NAME, "my-service",
                             ConfigMetadata::Origin::CODE)}},
        };

    Configuration cfg;
    cfg.products.emplace_back(std::move(product));

    auto scheduler2 = std::make_shared<FakeEventScheduler>();
    auto telemetry2 = Telemetry::create(*finalize_config(cfg), tracer_signature,
                                        logger, client, scheduler2, *url);

    // Simulate a remote config update overriding SERVICE_NAME
    telemetry2->capture_configuration_change(
        {{ConfigName::SERVICE_NAME, "rc-service",
          ConfigMetadata::Origin::REMOTE_CONFIG}});
    telemetry2->send_configuration_change();

    client->clear();
    scheduler2->trigger_extended_heartbeat();

    auto payload = nlohmann::json::parse(client->request_body);
    REQUIRE(is_valid_telemetry_payload(payload));

    auto ext_hb = find_payload(payload["payload"], "app-extended-heartbeat");
    REQUIRE(ext_hb.has_value());

    auto& configuration = (*ext_hb)["payload"]["configuration"];
    REQUIRE(configuration.is_array());

    bool found = false;
    for (const auto& entry : configuration) {
      if (entry["name"] == "service") {
        found = true;
        CHECK(entry["value"] == "rc-service");
        CHECK(entry["origin"] == "remote_config");
      }
    }
    CHECK(found);
  }

  SECTION("metrics reporting") {
    SECTION("counter and rate handles are sent with the metrics") {
      client->clear();
      auto requests = telemetry->metrics().counter<ErrorTags>(
          {"handle_requests", "handle-test", true},
          {"integration_name:datadog"});
      auto rate =
          telemetry->metrics().rate({"handle_rate", "handle-test", false});
      requests->record({true}, 1);
      requests->record({true}, 2);
      rate->record({}, 7);

      scheduler->trigger_metrics_capture();
      scheduler->trigger_heartbeat();

      auto message_batch = nlohmann::json::parse(client->request_body);
      auto generate_metrics =
          find_payload(message_batch["payload"], "generate-metrics");
      REQUIRE(generate_metrics);
      std::vector<nlohmann::json> handle_series;
      for (const auto& series : (*generate_metrics)["payload"]["series"]) {
        if (series["namespace"] == "handle-test") {
          handle_series.push_back(series);
        }
      }

      const auto expected = nlohmann::json::parse(R"([
        {
          "common": true,
          "metric": "handle_requests",
          "namespace": "handle-test",
          "points": [[1672484400, 3]],
          "tags": ["integration_name:datadog", "error:true"],
          "type": "count"
        },
        {
          "common": false,
          "metric": "handle_rate",
          "namespace": "handle-test",
          "points": [[1672484400, 7]],
          "type": "rate"
        }
      ])");
      REQUIRE(handle_series.size() == 2);
      for (const auto& series : expected) {
        CHECK(std::count(handle_series.begin(), handle_series.end(), series) ==
              1);
      }
    }

    SECTION("distribution handles are sent as sketches") {
      client->clear();
      auto duration = telemetry->metrics().distribution<ErrorTags>(
          {"handle_duration", "handle-test", true});
      duration->record({false}, 100);
      duration->record({false}, 250);

      scheduler->trigger_heartbeat();

      auto message_batch = nlohmann::json::parse(client->request_body);
      auto sketches = find_payload(message_batch["payload"], "sketches");
      REQUIRE(sketches);
      auto expected = nlohmann::json::parse(R"([
        {
          "common": true,
          "metric": "handle_duration",
          "namespace": "handle-test",
          "tags": ["error:false"]
        }
      ])");
      expected[0]["sketch_b64"] = sketch_b64({100, 250});
      CHECK(series_in(*sketches, "handle-test") == expected);
    }

    SECTION("dtor sends metrics and distributions") {
      // The handles outlive the Telemetry, which sends them when destroyed.
      std::shared_ptr<CounterHandle<>> counter;
      std::shared_ptr<RateHandle<>> rate;
      std::shared_ptr<DistributionHandle<>> distribution;
      {
        auto tmp_telemetry =
            Telemetry::create(*finalize_config(), tracer_signature, logger,
                              client, scheduler, *url, clock);
        counter = tmp_telemetry->metrics().counter(
            {"my_counter", "counter-test", true});
        rate = tmp_telemetry->metrics().rate({"request", "rate-test", true});
        distribution = tmp_telemetry->metrics().distribution(
            {"response_time", "dist-test", false});
        counter->record({}, 1);
        rate->record({}, 1000);
        distribution->record({}, 128);
        client->clear();
      }

      auto message_batch = nlohmann::json::parse(client->request_body);
      REQUIRE(is_valid_telemetry_payload(message_batch) == true);
      auto generate_metrics =
          find_payload(message_batch["payload"], "generate-metrics");
      REQUIRE(generate_metrics);
      const auto expected_metrics = nlohmann::json::parse(R"([
        {
          "common": true,
          "metric": "my_counter",
          "namespace": "counter-test",
          "points": [[1672484400, 1]],
          "type": "count"
        },
        {
          "common": true,
          "metric": "request",
          "namespace": "rate-test",
          "points": [[1672484400, 1000]],
          "type": "rate"
        }
      ])");
      const auto& sent = (*generate_metrics)["payload"]["series"];
      for (const auto& series : expected_metrics) {
        CHECK(std::count(sent.begin(), sent.end(), series) == 1);
      }

      auto sketches = find_payload(message_batch["payload"], "sketches");
      REQUIRE(sketches);
      auto expected_sketches = nlohmann::json::parse(R"([
        {
          "common": false,
          "metric": "response_time",
          "namespace": "dist-test"
        }
      ])");
      expected_sketches[0]["sketch_b64"] = sketch_b64({128});
      CHECK(series_in(*sketches, "dist-test") == expected_sketches);
    }
  }

  SECTION("logs reporting") {
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
      test_case.apply(*telemetry, test_case.input, test_case.stacktrace);
      scheduler->trigger_heartbeat();

      auto message_batch = nlohmann::json::parse(client->request_body);
      REQUIRE(is_valid_telemetry_payload(message_batch));
      REQUIRE(message_batch["payload"].size() >= 2);

      auto logs_message = find_payload(message_batch["payload"], "logs");
      REQUIRE(logs_message);

      auto logs_payload = (*logs_message)["payload"]["logs"];
      REQUIRE(logs_payload.size() == 1);
      CHECK(logs_payload[0]["level"] == test_case.expected_log_level);
      CHECK(logs_payload[0]["message"] == test_case.input);
      CHECK(logs_payload[0]["tracer_time"] == 1672484400);

      if (test_case.stacktrace) {
        CHECK(logs_payload[0]["stack_trace"] == test_case.stacktrace);
      } else {
        CHECK(logs_payload[0].contains("stack_trace") == false);
      }

      // Make sure the next heartbeat doesn't contains counters if no
      // datapoint has been incremented, decremented or set.
      client->clear();
      scheduler->trigger_heartbeat();

      auto message_batch2 = nlohmann::json::parse(client->request_body);
      REQUIRE(is_valid_telemetry_payload(message_batch2) == true);
      REQUIRE(message_batch2["payload"].size() >= 1);
      CHECK(find_payload(message_batch["payload"], "app-heartbeat"));
    }

    SECTION("dtor sends logs in `app-closing` message") {
      {
        auto tmp_telemetry =
            Telemetry::create(*finalize_config(), tracer_signature, logger,
                              client, scheduler, *url, clock);
        tmp_telemetry->log_warning("Be careful!");
        client->clear();
      }

      auto message_batch = nlohmann::json::parse(client->request_body);
      REQUIRE(is_valid_telemetry_payload(message_batch));
      REQUIRE(message_batch["payload"].size() >= 2);

      auto logs_message = find_payload(message_batch["payload"], "logs");
      REQUIRE(logs_message);

      auto logs_payload = (*logs_message)["payload"]["logs"];
      REQUIRE(logs_payload.size() == 1);
      CHECK(logs_payload[0]["level"] == "WARNING");
      CHECK(logs_payload[0]["message"] == "Be careful!");
      CHECK(logs_payload[0]["tracer_time"] == 1672484400);
    }
  }
}

TELEMETRY_IMPLEMENTATION_TEST("Tracer telemetry configuration") {
  // Cases:
  //  - when `report_metrics` is set to false. No metrics are reported.
  //  - when `report_logs` is set to false. No logs are reported.
  //  - respects interval defined.
  //  - telemetry disabled doesn't send anything.

  auto logger = std::make_shared<MockLogger>();
  auto client = std::make_shared<MockHTTPClient>();
  auto scheduler = std::make_shared<FakeEventScheduler>();

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

    auto telemetry = Telemetry::create(*final_cfg, tracer_signature, logger,
                                       client, scheduler, *url);
    CHECK(scheduler->metrics_callback == nullptr);
    CHECK(scheduler->metrics_interval == nullopt);
  }

  SECTION("intervals are respected") {
    Configuration cfg;
    cfg.metrics_interval_seconds = .5;
    cfg.heartbeat_interval_seconds = 30;

    auto final_cfg = finalize_config(cfg);
    REQUIRE(final_cfg);

    auto telemetry = Telemetry::create(*final_cfg, tracer_signature, logger,
                                       client, scheduler, *url);
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

    auto telemetry = Telemetry::create(*final_cfg, tracer_signature, logger,
                                       client, scheduler, *url);
    telemetry->log_error("error");

    // NOTE(@dmehala): logs are sent with an heartbeat.
    scheduler->trigger_heartbeat();

    auto message_batch = nlohmann::json::parse(client->request_body);
    REQUIRE(is_valid_telemetry_payload(message_batch));
    REQUIRE(message_batch["payload"].size() >= 1);
    CHECK(find_payload(message_batch["payload"], "app-heartbeat"));
  }
}
