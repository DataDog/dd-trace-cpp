#pragma once

// This component provides a class, TracerTelemetry, that is used to collect
// data from the activity of the tracer implementation, and encode messages that
// can be submitted to the Datadog Agent.
//
// Counter metrics are updated in other parts of the tracers, with the values
// being managed by this class.
//
// The messages that TracerTelemetry produces are
// - `app-started`
// - `message-batch`
// - `app-heartbeat`
// - `generate-metrics`
// - `app-closing`
//
// `app-started` messages are sent as part of initializing the tracer.
//
// At 60 second intervals, a `message-batch` message is sent containing an
// `app-heartbeat` message, and if metrics have changed during that interval, a
// `generate-metrics` message is also included in the batch.
//
// `app-closing` messages are sent as part of terminating the tracer. These are
// sent as a `message-batch` message , and if metrics have changed since the
// last `app-heartbeat` event, a `generate-metrics` message is also included in
// the batch.
//
#include <vector>

#include "clock.h"
#include "json.hpp"
#include "metrics.h"
#include "runtime_id.h"

namespace datadog {
namespace tracing {

class Logger;
struct SpanDefaults;

class TracerTelemetry {
  bool enabled_ = false;
  bool debug_ = false;
  Clock clock_;
  std::shared_ptr<Logger> logger_;
  std::shared_ptr<const SpanDefaults> span_defaults_;
  RuntimeID runtime_id_;
  std::string hostname_;
  uint64_t seq_id_ = 0;
  // This structure contains all the metrics that are exposed by tracer
  // telemetry.
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
  // Each metric has an associated MetricSnapshot that contains the data points,
  // represented as a timestamp and the value of that metric.
  using MetricSnapshot = std::vector<std::pair<std::time_t, uint64_t>>;
  // This uses a reference_wrapper so references to internal metric values can
  // be captured, and be iterated trivially when the values need to be
  // snapshotted and published in telemetry messages.
  std::vector<std::pair<std::reference_wrapper<Metric>, MetricSnapshot>>
      metrics_snapshots_;

  nlohmann::json generate_telemetry_body(std::string request_type);

 public:
  TracerTelemetry(bool enabled, const Clock& clock,
                  const std::shared_ptr<Logger>& logger,
                  const std::shared_ptr<const SpanDefaults>& span_defaults,
                  const RuntimeID& runtime_id);
  bool enabled() { return enabled_; };
  // Provides access to the telemetry metrics for updating the values.
  // This value should not be stored.
  auto& metrics() { return metrics_; };
  // Constructs an `app-started` message using information provided when
  // constructed and the tracer_config value passed in.
  std::string app_started(nlohmann::json&& tracer_config);
  // This is used to take a snapshot of the current state of metrics and collect
  // timestamped "points" of values. These values are later submitted in
  // `generate-metrics` messages.
  void capture_metrics();
  // Constructs a messsage-batch containing `app-heartbeat`, and if metrics have
  // been modified, a `generate-metrics` message.
  std::string heartbeat_and_telemetry();
  // Constructs a message-batch containing `app-closing`, and if metrics have
  // been modified, a `generate-metrics` message.
  std::string app_closing();
};

}  // namespace tracing
}  // namespace datadog
