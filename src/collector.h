#pragma once

#include <memory>
#include <optional>
#include <vector>

#include "error.h"

namespace datadog {
namespace tracing {

class SpanData;
class TraceSampler;

class Collector {
 public:
  virtual std::optional<Error> send(
      std::vector<std::unique_ptr<SpanData>>&& spans,
      const std::shared_ptr<TraceSampler>& response_handler) = 0;
};

}  // namespace tracing
}  // namespace datadog
