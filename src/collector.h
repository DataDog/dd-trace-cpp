#pragma once

#include <functional>
#include <optional>
#include <vector>

#include "error.h"
#include "span_data.h"

namespace datadog {
namespace tracing {

struct CollectorResponse {
  // TODO
};

class Collector {
 public:
  virtual std::optional<Error> send(
      std::vector<SpanData>&& spans,
      const std::function<void(const CollectorResponse&)>& callback) = 0;
};

}  // namespace tracing
}  // namespace datadog