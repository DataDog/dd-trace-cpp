#pragma once

#include "collector.h"

namespace datadog {
namespace tracing {

class NullCollector : public Collector {
 public:
  Expected<void> send(std::vector<std::unique_ptr<SpanData>>&&,
                      const std::shared_ptr<TraceSampler>&) override {
    return {};
  }
};

}  // namespace tracing
}  // namespace datadog
