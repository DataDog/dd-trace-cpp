#pragma once

// This component defines the internal span-link representation and its
// MessagePack serialization. Applications use `SpanContext` and
// `Span::add_link` instead.

#include <datadog/span.h>

namespace datadog::tracing {

struct SpanLink {
  TraceID trace_id;
  std::uint64_t span_id = 0;
  Optional<std::string> tracestate;
  SpanLinkAttributes attributes;
  Optional<std::uint32_t> flags;
};

Expected<void> msgpack_encode(std::string& destination, const SpanLink& link);

}  // namespace datadog::tracing
