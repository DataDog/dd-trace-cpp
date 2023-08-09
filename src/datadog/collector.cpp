#include "collector.h"

namespace datadog {
namespace tracing {

Expected<void> Collector::send(
    std::vector<std::unique_ptr<SpanData>>&& spans,
    const std::shared_ptr<TraceSampler>& response_handler, Optional<Span>&&) {
  // The default implementation of the `Optional<Span>` debug overload just
  // ignores the `Optional<Span>` argument and forwards to the overload
  // without it.
  // This way, a `Collector` implementation need not implement trace
  // debugging.
  return send(std::move(spans), response_handler);
}

}  // namespace tracing
}  // namespace datadog
