// These are tests for `TracerTelemetry`. TracerTelemetry is used to measure
// activity in other parts of the tracer implementation, and construct messages
// that are sent to the datadog agent.

#include <datadog/span_defaults.h>
#include <datadog/tracer_telemetry.h>

#include <datadog/json.hpp>

#include "datadog/runtime_id.h"
#include "mocks/loggers.h"
#include "test.h"

using namespace datadog::tracing;

TEST_CASE("Tracer telemetry") {
  const std::time_t mock_time = 1672484400;
  const Clock clock = []() {
    TimePoint result;
    result.wall = std::chrono::system_clock::from_time_t(mock_time);
    return result;
  };
  auto logger = std::make_shared<MockLogger>();

  const TracerID tracer_id{/* runtime_id = */ RuntimeID::generate(),
                           /* service = */ "testsvc",
                           /* environment = */ "test"};

  TracerTelemetry tracer_telemetry = {true, clock, logger, tracer_id, {}};

  SECTION("generates app-started message") {
    auto app_started_message = tracer_telemetry.app_started();
    auto app_started = nlohmann::json::parse(app_started_message);
    REQUIRE(app_started["request_type"] == "app-started");
  }

  SECTION("generates a heartbeat message") {
    auto heartbeat_message = tracer_telemetry.heartbeat_and_telemetry();
    auto message_batch = nlohmann::json::parse(heartbeat_message);
    REQUIRE(message_batch["payload"].size() == 1);
    auto heartbeat = message_batch["payload"][0];
    REQUIRE(heartbeat["request_type"] == "app-heartbeat");
  }

  SECTION("captures metrics and sends generate-metrics payload") {
    tracer_telemetry.metrics().tracer.trace_segments_created_new.inc();
    REQUIRE(
        tracer_telemetry.metrics().tracer.trace_segments_created_new.value() ==
        1);
    tracer_telemetry.capture_metrics();
    REQUIRE(
        tracer_telemetry.metrics().tracer.trace_segments_created_new.value() ==
        0);
    auto heartbeat_and_telemetry_message =
        tracer_telemetry.heartbeat_and_telemetry();
    auto message_batch = nlohmann::json::parse(heartbeat_and_telemetry_message);
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
    auto app_closing_message = tracer_telemetry.app_closing();
    auto message_batch = nlohmann::json::parse(app_closing_message);
    REQUIRE(message_batch["payload"].size() == 1);
    auto heartbeat = message_batch["payload"][0];
    REQUIRE(heartbeat["request_type"] == "app-closing");
  }
}
