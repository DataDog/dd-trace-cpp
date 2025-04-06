#pragma once

#include <datadog/telemetry/metrics.h>

namespace datadog::telemetry {

// TODO: `enable_if_t` is same `Metric<T>`

/// `MetricContext` is a unique identifier for metrics.
/// It depends on the kind of metric, its name, scope, common and the set of
/// tags.
template <typename Metric>
struct MetricContext final {
  /// The metric definition.
  Metric id;
  /// Set of tags.
  std::vector<std::string> tags;

  std::size_t hash() const {
    using std::hash;
    using std::size_t;
    using std::string;

    std::size_t h = tags.size();
    h ^= hash<string>()(id.name);
    h ^= (hash<string>()(id.scope) << 1) >> 1;
    h ^= (hash<bool>()(id.common) << 1);
    for (const auto& t : tags) {
      h ^= hash<string>()(t) >> 16;
    }

    return h;
  }

  bool operator==(const MetricContext<Metric>& rhs) const {
    return id.name == rhs.id.name && id.scope == rhs.id.scope &&
           id.common == rhs.id.common && tags == rhs.tags;
  }
};

}  // namespace datadog::telemetry

template <>
struct std::hash<
    datadog::telemetry::MetricContext<datadog::telemetry::Counter>> {
  std::size_t operator()(
      const datadog::telemetry::MetricContext<datadog::telemetry::Counter>&
          counter) const {
    return counter.hash();
  }
};

template <>
struct std::hash<datadog::telemetry::MetricContext<datadog::telemetry::Rate>> {
  std::size_t operator()(
      const datadog::telemetry::MetricContext<datadog::telemetry::Rate>& rate)
      const {
    return rate.hash();
  }
};

template <>
struct std::hash<
    datadog::telemetry::MetricContext<datadog::telemetry::Distribution>> {
  std::size_t operator()(
      const datadog::telemetry::MetricContext<datadog::telemetry::Distribution>&
          distribution) const {
    return distribution.hash();
  }
};
