// This test covers operations defined for metrics defined in `metrics.h`.

#include <datadog/telemetry/metrics.h>

#include "test.h"

using namespace datadog::telemetry;

#define TELEMETRY_METRICS_TEST(x) \
  TEST_CASE(x, "[telemetry],[telemetry.metrics]")

TELEMETRY_METRICS_TEST("Counter metrics") {
  CounterMetric metric = {
      "test.counter.metric", "test_scope", {"testing-testing:123"}, true};

  metric.inc();
  metric.add(41);
  REQUIRE(metric.value() == 42);
  auto captured_value = metric.capture_and_reset_value();
  REQUIRE(captured_value == 42);
  REQUIRE(metric.value() == 0);
}

TELEMETRY_METRICS_TEST("Gauge metrics") {
  GaugeMetric metric = {
      "test.gauge.metric", "test_scope", {"testing-testing:123"}, true};
  metric.set(40);
  metric.inc();
  metric.add(10);
  metric.sub(8);
  metric.dec();
  REQUIRE(metric.value() == 42);
  auto captured_value = metric.capture_and_reset_value();
  REQUIRE(captured_value == 42);
  REQUIRE(metric.value() == 0);

  metric.add(10);
  metric.sub(11);
  REQUIRE(metric.value() == 0);
}
