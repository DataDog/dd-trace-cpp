#pragma once

#include "collector.h"

namespace datadog {
namespace tracing {

struct DatadogAgentConfig;

class DatadogAgent : public Collector {
  // TODO
 public:
  explicit DatadogAgent(const DatadogAgentConfig& config);

  virtual std::optional<Error> send(
      std::vector<std::unique_ptr<SpanData>>&& spans,
      const std::function<void(const CollectorResponse&)>& callback) override;
};

}  // namespace tracing
}  // namespace datadog
