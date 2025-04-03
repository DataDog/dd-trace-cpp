#pragma once

// This component provides an interface, `Metric`, and specific classes for
// Counter and Gauge metrics. A metric has a name, type, and set of key:value
// tags associated with it. Metrics can be general to APM or language-specific.
// General metrics have `common` set to `true`, and language-specific metrics
// have `common` set to `false`.

#include <atomic>
#include <string>
#include <vector>

namespace datadog {
namespace telemetry {

class Metric {
  // The name of the metric that will be published. A transformation occurs
  // based on the name and whether it is "common" or "language-specific" when it
  // is recorded.
  std::string name_;
  // The type of the metric. This will currently be count or gauge.
  std::string type_;
  // Namespace of the metric.
  std::string scope_;
  // Tags associated with this specific instance of the metric.
  std::vector<std::string> tags_;
  // This affects the transformation of the metric name, where it can be a
  // common telemetry metric, or a language-specific metric that is prefixed
  // with the language name.
  bool common_;

 protected:
  std::atomic<uint64_t> value_ = 0;
  Metric(std::string name, std::string type, std::string scope,
         std::vector<std::string> tags, bool common);

  Metric(Metric&& rhs)
      : name_(std::move(rhs.name_)),
        type_(std::move(rhs.type_)),
        scope_(std::move(rhs.scope_)),
        tags_(std::move(rhs.tags_)) {
    rhs.value_.store(value_.exchange(rhs.value_));
  }

  Metric& operator=(Metric&& rhs) {
    if (&rhs != this) {
      std::swap(name_, rhs.name_);
      std::swap(type_, rhs.type_);
      std::swap(scope_, rhs.scope_);
      std::swap(tags_, rhs.tags_);
      rhs.value_.store(value_.exchange(rhs.value_));
    }
    return *this;
  }

 public:
  // Accessors for name, type, tags, common and capture_and_reset_value are used
  // when producing the JSON message for reporting metrics.
  std::string name();
  std::string type();
  std::string scope();
  std::vector<std::string> tags();
  bool common();
  uint64_t value();
  uint64_t capture_and_reset_value();
};

// A count metric is used for measuring activity, and has methods for adding a
// number of actions, or incrementing the current number of actions by 1.
class CounterMetric : public Metric {
 public:
  CounterMetric(std::string name, std::string scope,
                std::vector<std::string> tags, bool common);
  void inc();
  void add(uint64_t amount);
};

// A gauge metric is used for measuring state, and mas methods to set the
// current state, add or subtract from it, or increment/decrement the current
// state by 1.
class GaugeMetric : public Metric {
 public:
  GaugeMetric(std::string name, std::string scope,
              std::vector<std::string> tags, bool common);
  void set(uint64_t value);
  void inc();
  void add(uint64_t amount);
  void dec();
  void sub(uint64_t amount);
};

// This structure contains all the metrics that are exposed by tracer
// telemetry.
struct DefaultMetrics {
  struct {
    telemetry::CounterMetric spans_created = {
        "spans_created", "tracers", {}, true};
    telemetry::CounterMetric spans_finished = {
        "spans_finished", "tracers", {}, true};

    telemetry::CounterMetric trace_segments_created_new = {
        "trace_segments_created", "tracers", {"new_continued:new"}, true};
    telemetry::CounterMetric trace_segments_created_continued = {
        "trace_segments_created", "tracers", {"new_continued:continued"}, true};
    telemetry::CounterMetric trace_segments_closed = {
        "trace_segments_closed", "tracers", {}, true};
    telemetry::CounterMetric baggage_items_exceeded = {
        "context_header.truncated",
        "tracers",
        {{"truncation_reason:baggage_item_count_exceeded"}},
        true,
    };
    telemetry::CounterMetric baggage_bytes_exceeded = {
        "context_header.truncated",
        "tracers",
        {{"truncation_reason:baggage_byte_count_exceeded"}},
        true,
    };
  } tracer;
  struct {
    telemetry::CounterMetric requests = {
        "trace_api.requests", "tracers", {}, true};

    telemetry::CounterMetric responses_1xx = {
        "trace_api.responses", "tracers", {"status_code:1xx"}, true};
    telemetry::CounterMetric responses_2xx = {
        "trace_api.responses", "tracers", {"status_code:2xx"}, true};
    telemetry::CounterMetric responses_3xx = {
        "trace_api.responses", "tracers", {"status_code:3xx"}, true};
    telemetry::CounterMetric responses_4xx = {
        "trace_api.responses", "tracers", {"status_code:4xx"}, true};
    telemetry::CounterMetric responses_5xx = {
        "trace_api.responses", "tracers", {"status_code:5xx"}, true};

    telemetry::CounterMetric errors_timeout = {
        "trace_api.errors", "tracers", {"type:timeout"}, true};
    telemetry::CounterMetric errors_network = {
        "trace_api.errors", "tracers", {"type:network"}, true};
    telemetry::CounterMetric errors_status_code = {
        "trace_api.errors", "tracers", {"type:status_code"}, true};

  } trace_api;
};

}  // namespace telemetry
}  // namespace datadog
