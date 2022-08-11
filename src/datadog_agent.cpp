#include "datadog_agent.h"

namespace datadog {
namespace tracing {

std::optional<Error> DatadogAgent::send(
    std::vector<SpanData>&&,
    const std::function<void(const CollectorResponse&)>&) {
  // TODO
  return std::nullopt;
}

}  // namespace tracing
}  // namespace datadog
