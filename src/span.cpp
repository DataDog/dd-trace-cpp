#include "span.h"

#include <cassert>

namespace datadog {
namespace tracing {

Span::Span(SpanData* data, const std::shared_ptr<TraceSegment>& trace_segment,
           const std::function<std::uint64_t()>& generate_span_id,
           const Clock& clock)
    : data_(data),
      trace_segment_(trace_segment),
      generate_span_id_(generate_span_id),
      clock_(clock) {
  assert(data);
  assert(trace_segment);
  assert(generate_span_id);
  assert(clock);
}

// TODO

}  // namespace tracing
}  // namespace datadog
