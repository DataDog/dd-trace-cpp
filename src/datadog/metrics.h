#pragma once

#include <atomic>
#include <string>
#include <vector>

namespace datadog {
namespace tracing {

class Metric {
  const std::string name_;
  const std::string type_;
  const std::vector<std::string> tags_;
  bool common_;

 protected:
  std::atomic<uint64_t> value_ = 0;
  Metric(const std::string name, std::string type,
         const std::vector<std::string> tags, bool common);

 public:
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
