#pragma once
#include <memory>
#include <vector>

#include "clock.h"
#include "json_fwd.hpp"
#include "metrics.h"

namespace datadog {
namespace tracing {

class Logger;
class SpanDefaults;

class TracerTelemetry {
  bool enabled_ = false;
  bool debug_ = false;
  Clock clock_;
  std::shared_ptr<Logger> logger_;
  std::shared_ptr<const SpanDefaults> span_defaults_;
  std::string hostname_;
  uint64_t seq_id = 0;
  using MetricSnapshot = std::vector<std::pair<time_t, uint64_t>>;
  // This uses a reference_wrapper so references to internal metric values can
  // be captured, and be iterated trivially when the values need to be
  // snapshotted and published in telemetry messages.
  std::vector<std::pair<std::reference_wrapper<Metric>, MetricSnapshot>>
      metrics_snapshots_;
  struct {
    struct {
      CounterMetric spans_created = {
          "spans_created", {"integration_name:datadog"}, true};
      CounterMetric spans_finished = {
          "spans_finished", {"integration_name:datadog"}, true};

      CounterMetric trace_segments_created_new = {
          "trace_segments_created", {"new_continued:new"}, true};
      CounterMetric trace_segments_created_continued = {
          "trace_segments_created", {"new_continued:continued"}, true};
      CounterMetric trace_segments_closed = {
          "trace_segments_closed", {"integration_name:datadog"}, true};
    } tracer;
    struct {
      CounterMetric requests = {"trace_api.requests", {}, true};

      CounterMetric responses_1xx = {
          "trace_api.responses", {"status_code:1xx"}, true};
      CounterMetric responses_2xx = {
          "trace_api.responses", {"status_code:2xx"}, true};
      CounterMetric responses_3xx = {
          "trace_api.responses", {"status_code:3xx"}, true};
      CounterMetric responses_4xx = {
          "trace_api.responses", {"status_code:4xx"}, true};
      CounterMetric responses_5xx = {
          "trace_api.responses", {"status_code:5xx"}, true};

      CounterMetric errors_timeout = {
          "trace_api.errors", {"type:timeout"}, true};
      CounterMetric errors_network = {
          "trace_api.errors", {"type:network"}, true};
      CounterMetric errors_status_code = {
          "trace_api.errors", {"type:status_code"}, true};

    } trace_api;
  } metrics_;

 public:
  TracerTelemetry(bool enabled, const Clock& clock,
                  const std::shared_ptr<Logger>& logger,
                  const std::shared_ptr<const SpanDefaults>& span_defaults);
  bool enabled() { return enabled_; };
  auto& metrics() { return metrics_; };
  std::string app_started(nlohmann::json&& tracer_config);
  void capture_metrics();
  std::string heartbeat_and_telemetry();
  std::string app_closing();
};

}  // namespace tracing
}  // namespace datadog