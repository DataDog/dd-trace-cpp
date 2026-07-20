#pragma once

// This component defines `SpanContext`, the identifying and propagation state
// of a span. It can be supplied to `Span::add_link` to associate a span with a
// span in this or another trace.

#include <unordered_map>

#include "trace_id.h"

namespace datadog::tracing {

class SpanContext {
 public:
  // 128-bit trace ID of the span.
  TraceID trace_id;
  // ID of the span within its trace.
  std::uint64_t span_id;
  // W3C `tracestate` header value, if any.
  Optional<std::string> tracestate;
  // W3C trace flags, if any.
  Optional<std::uint32_t> flags;

  SpanContext(TraceID trace_id, std::uint64_t span_id,
              Optional<std::string> tracestate = nullopt,
              Optional<std::uint32_t> flags = nullopt)
      : trace_id(trace_id),
        span_id(span_id),
        tracestate(tracestate),
        flags(flags) {}
};

}  // namespace datadog::tracing
