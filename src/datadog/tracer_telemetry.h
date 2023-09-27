#pragma once
#include <vector>

#include "clock.h"
#include "metrics.h"
#include "tracer_config.h"

namespace datadog {
namespace tracing {
class TracerTelemetry {
  Clock clock_;
  FinalizedTracerConfig config_;
  uint64_t seq_id = 0;
  std::vector<std::pair<std::reference_wrapper<CounterMetric>, std::vector<std::pair<time_t, uint64_t>>>> counter_metrics_;
  std::vector<std::pair<std::reference_wrapper<GaugeMetric>, std::vector<std::pair<time_t, uint64_t>>>> gauge_metrics_;
  using MetricSnapshot = std::vector<std::pair<time_t, uint64_t>>;
  std::vector<std::pair<std::reference_wrapper<Metric>, MetricSnapshot>> metrics_;

  CounterMetric traces_started_ = {"traces_started", true};
  CounterMetric traces_finished_ = {"traces_finished", true};
  GaugeMetric active_traces_ = {"active_traces", true};

  CounterMetric spans_started_ = {"spans_started", true};
  CounterMetric spans_finished_ = {"spans_finished", true};
  GaugeMetric active_spans_ = {"active_spans", true};

  CounterMetric trace_api_requests_ = {"trace_api.requests", true};

 public:
  TracerTelemetry(const Clock& clock, const FinalizedTracerConfig& config);
  std::string appStarted();
  void captureMetrics();
  std::string heartbeatAndTelemetry();

  CounterMetric& traces_started() { return traces_started_; };
  CounterMetric& traces_finished() { return traces_finished_; };
  GaugeMetric& active_traces() { return active_traces_; };

  CounterMetric& spans_started() { return spans_started_; };
  CounterMetric& spans_finished() { return spans_finished_; };
  GaugeMetric& active_spans() { return active_spans_; };

  CounterMetric& trace_api_requests() { return trace_api_requests_; };
};

}  // namespace tracing
}  // namespace datadog
