#pragma once

#include <functional>
#include <memory>
#include <optional>
#include <vector>

#include "error.h"

namespace datadog {
namespace tracing {

class CollectorResponse;
class SpanData;

class Collector {
 public:
  virtual std::optional<Error> send(
      std::vector<std::unique_ptr<SpanData>>&& spans,
      const std::function<void(CollectorResponse)>& callback) = 0;
};

}  // namespace tracing
}  // namespace datadog
