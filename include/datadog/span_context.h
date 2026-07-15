#pragma once

// This component defines `SpanContext`, the identifying and propagation state
// of a span. It can be supplied to `Span::add_link` to associate a span with a
// span in this or another trace.

#include <cstdint>
#include <string>
#include <unordered_map>

#include "optional.h"
#include "trace_id.h"

namespace datadog::tracing {

// The map type used for user-supplied span-link attributes.
using SpanLinkAttributes = std::unordered_map<std::string, std::string>;

struct SpanContext {
  // 128-bit trace ID of the span.
  TraceID trace_id;
  // ID of the span within its trace.
  std::uint64_t span_id = 0;
  // W3C `tracestate` header value, if any.
  Optional<std::string> tracestate;
  // W3C trace flags, if any.
  Optional<std::uint32_t> flags;
};

}  // namespace datadog::tracing
