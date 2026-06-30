#pragma once

// This component defines `SpanLink`, a causal association between the span that
// owns the link and another span (possibly in another trace). Span links carry
// the linked span's identifiers plus optional W3C tracestate, trace flags, and
// arbitrary string attributes. They are serialized into the owning span under
// the `span_links` key in a format shared by all Datadog tracers.

#include <cstdint>
#include <string>
#include <unordered_map>

#include "expected.h"
#include "optional.h"
#include "trace_id.h"

namespace datadog {
namespace tracing {

struct SpanLink {
  // 128-bit trace ID of the linked span.
  TraceID trace_id;
  // The linked span's ID.
  std::uint64_t span_id = 0;
  // W3C `tracestate` header value from the linked context, if any.
  Optional<std::string> tracestate;
  // Additional string attributes associated with the link.
  std::unordered_map<std::string, std::string> attributes;
  // W3C trace flags from the linked context, if any.
  Optional<std::uint32_t> flags;
};

// Append to the specified `destination` the MessagePack representation of the
// specified `link`.
Expected<void> msgpack_encode(std::string& destination, const SpanLink& link);

}  // namespace tracing
}  // namespace datadog
