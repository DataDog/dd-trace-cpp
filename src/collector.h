#pragma once

#include "error.h"
#include "span_data.h"

#include <functional>
#include <optional>
#include <vector>

namespace datadog {
namespace tracing {

struct CollectorResponse {
    // TODO
};

class Collector {
  public:
    virtual std::optional<Error> send(std::vector<SpanData>&& spans, std::function<void(const CollectorResponse&)>) = 0;
};

}  // namespace tracing
}  // namespace datadog
