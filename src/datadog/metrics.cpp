#include "metrics.h"

#include "json.hpp"

namespace datadog {
namespace tracing {

Metric::Metric(std::string name, std::string type, bool common) : name_(name), type_(type), common_(common) {}
std::string Metric::name() { return name_; }
std::string Metric::type() { return type_; }
bool Metric::common() { return common_; }
uint64_t Metric::value() { return value_; }

CounterMetric::CounterMetric(std::string name, bool common) : Metric(name, "count", common) {}
void CounterMetric::inc() { add(1); }
void CounterMetric::add(uint64_t amount) { value_ += amount; }

GaugeMetric::GaugeMetric(std::string name, bool common) : Metric(name, "gauge", common) {}
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
