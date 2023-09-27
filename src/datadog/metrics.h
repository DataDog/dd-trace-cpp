#pragma once

#include <atomic>

#include "json_fwd.hpp"
#include "string_view.h"

namespace datadog {
namespace tracing {

class Metric {
  std::string name_;
  std::string type_;
  bool common_;
  protected:
  std::atomic<uint64_t> value_ = 0;
  Metric(std::string name, std::string type, bool common);
 public:
  std::string name();
  std::string type();
  bool common();
  uint64_t value();
};

class CounterMetric : public Metric {
 public:
  CounterMetric(std::string name, bool common);
  void inc();
  void add(uint64_t amount);
};

class GaugeMetric : public Metric {
 public:
  GaugeMetric(std::string name, bool common);
  void set(uint64_t value);
  void inc();
  void add(uint64_t amount);
  void dec();
  void sub(uint64_t amount);
};

}  // namespace tracing
}  // namespace datadog
