#include <datadog/metrics.h>

#include <random>

#include "json.hpp"

namespace datadog {
namespace tracing {
namespace {
uint16_t rand_0_1000() {
  static std::mt19937 rng;
  static std::uniform_int_distribution<uint16_t> udist(0, 1000);
  return udist(rng);
}
}  // namespace

Metric::Metric(std::string tel_namespace, std::string name, std::string type,
               std::vector<std::string> tags, bool common)
    : name_(name),
      type_(type),
      tags_(tags),
      common_(common),
      namespace_(tel_namespace) {}
std::string Metric::name() { return name_; }
std::string Metric::type() { return type_; }
std::string Metric::tel_namespace() { return namespace_; }
std::vector<std::string> Metric::tags() { return tags_; }
bool Metric::common() { return common_; }
uint64_t Metric::value() { return value_; }
uint64_t Metric::capture_and_reset_value() { return value_.exchange(0); }

CounterMetric::CounterMetric(std::string name, std::vector<std::string> tags,
                             bool common, std::string tel_namespace)
    : Metric(tel_namespace, name, "count", tags, common) {}
void CounterMetric::inc() { add(1); }
void CounterMetric::add(uint64_t amount) { value_ += amount; }

GaugeMetric::GaugeMetric(std::string name, std::vector<std::string> tags,
                         bool common, std::string tel_namespace)
    : Metric(tel_namespace, name, "gauge", tags, common) {}
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

HistogramMetric::HistogramMetric(std::string name,
                                 std::vector<std::string> tags, bool common,
                                 std::string tel_namespace)
    : Metric(tel_namespace, name, "distribution", tags, common) {
  values_.reserve(HistogramMetric::max_size_);
}
void HistogramMetric::set(uint64_t value) {
  if (values_.size() > HistogramMetric::max_size_) {
    values_[rand_0_1000() % HistogramMetric::max_size_] = value;
  } else {
    values_.push_back(value);
  }
}
std::vector<uint64_t> HistogramMetric::capture_and_reset_values() {
  auto res = values_;
  values_.clear();
  return res;
}

}  // namespace tracing
}  // namespace datadog
