#pragma once

#include <atomic>
#include <string>
#include <vector>

namespace datadog {
namespace tracing {

class Metric {
  // The name of the metric that will be published. A transformation occurs
  // based on the name and whether it is "common" or "language-specific" when it
  // is recorded.
  const std::string name_;
  // The type of the metric. This will currently be count or gauge.
  const std::string type_;
  // Tags associated with this specific instance of the metric.
  const std::vector<std::string> tags_;
  // This affects the transformation of the metric name, where it can be a
  // common telemetry metric, or a language-specific metric that is prefixed
  // with the language name.
  bool common_;

 protected:
  std::atomic<uint64_t> value_ = 0;
  Metric(const std::string name, std::string type,
         const std::vector<std::string> tags, bool common);

 public:
  // Accessors for name, type, tags, common and value are used when producing
  // the JSON message for reporting metrics.
  const std::string name();
  const std::string type();
  const std::vector<std::string> tags();
  bool common();
  uint64_t value();
};

class CounterMetric : public Metric {
 public:
  CounterMetric(const std::string name, const std::vector<std::string> tags,
                bool common);
  void inc();
  void add(uint64_t amount);
};

class GaugeMetric : public Metric {
 public:
  GaugeMetric(const std::string name, const std::vector<std::string> tags,
              bool common);
  void set(uint64_t value);
  void inc();
  void add(uint64_t amount);
  void dec();
  void sub(uint64_t amount);
};

}  // namespace tracing
}  // namespace datadog
