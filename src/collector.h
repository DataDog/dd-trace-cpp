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
      std::function<void(const CollectorResponse&)>) = 0;
};

}  // namespace tracing
}  // namespace datadog
