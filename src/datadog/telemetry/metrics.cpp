#include <datadog/telemetry/metrics.h>

namespace datadog {
namespace telemetry {

Metric::Metric(std::string name, std::string type, std::string scope,
               std::vector<std::string> tags, bool common)
    : name_(std::move(name)),
      type_(std::move(type)),
      scope_(std::move(scope)),
      tags_(std::move(tags)),
      common_(common) {}
std::string Metric::name() { return name_; }
std::string Metric::type() { return type_; }
std::string Metric::scope() { return scope_; }
std::vector<std::string> Metric::tags() { return tags_; }
bool Metric::common() { return common_; }
uint64_t Metric::value() { return value_; }
uint64_t Metric::capture_and_reset_value() { return value_.exchange(0); }

CounterMetric::CounterMetric(std::string name, std::string scope,
                             std::vector<std::string> tags, bool common)
    : Metric(name, "count", scope, tags, common) {}
void CounterMetric::inc() { add(1); }
void CounterMetric::add(uint64_t amount) { value_ += amount; }

GaugeMetric::GaugeMetric(std::string name, std::string scope,
                         std::vector<std::string> tags, bool common)
    : Metric(name, "gauge", scope, tags, common) {}
void GaugeMetric::set(uint64_t value) { value_ = value; }
void GaugeMetric::inc() { add(1); }
void GaugeMetric::add(uint64_t amount) { value_ += amount; }
void GaugeMetric::dec() { sub(1); }
void GaugeMetric::sub(uint64_t amount) {
  if (amount > value_) {
    value_ = 0;
  } else {
    value_ -= amount;
  }
}

}  // namespace telemetry
}  // namespace datadog
