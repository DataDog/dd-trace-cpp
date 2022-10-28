#pragma once

// This component provides a `class`, `NullCollector`, that implements the
// `Collector` interface in terms of a no-op. It's used by `Tracer` in lieu
// of a `DatadogAgent` whenever `TracerConfig::report_traces` is `false`.

#include "collector.h"

namespace datadog {
namespace tracing {

class NullCollector : public Collector {
 public:
  Expected<void> send(std::vector<std::unique_ptr<SpanData>>&&,
                      const std::shared_ptr<TraceSampler>&) override {
    return {};
  }

  void config_json(nlohmann::json& destination) const override;
};

}  // namespace tracing
}  // namespace datadog
