#include "metrics.h"

#include "json.hpp"

namespace datadog {
namespace tracing {

Metric::Metric(const std::string name, std::string type,
               const std::vector<std::string> tags, bool common)
    : name_(name), type_(type), tags_(tags), common_(common) {}
const std::string Metric::name() { return name_; }
const std::string Metric::type() { return type_; }
const std::vector<std::string> Metric::tags() { return tags_; }
bool Metric::common() { return common_; }
uint64_t Metric::value() { return value_; }

CounterMetric::CounterMetric(const std::string name,
                             const std::vector<std::string> tags, bool common)
    : Metric(name, "count", tags, common) {}
void CounterMetric::inc() { add(1); }
void CounterMetric::add(uint64_t amount) { value_ += amount; }

GaugeMetric::GaugeMetric(const std::string name,
                         const std::vector<std::string> tags, bool common)
    : Metric(name, "gauge", tags, common) {}
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

}  // namespace tracing
}  // namespace datadog
