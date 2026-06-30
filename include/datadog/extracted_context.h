#pragma once

// Tracing context extracted from incoming distributed headers. Unlike `Span`,
// this type holds only the propagation fields of the upstream context and does
// not represent a locally-owned span.

#include <cstdint>
#include <string>

#include "optional.h"
#include "trace_id.h"

namespace datadog {
namespace tracing {

struct ExtractedContext {
  TraceID trace_id;
  // The upstream span's ID (e.g. x-datadog-parent-id or traceparent
  // parent-id field).
  std::uint64_t span_id = 0;
  // W3C tracestate, if present in the incoming headers.
  Optional<std::string> tracestate;
  // W3C trace flags byte, if present in the incoming traceparent header.
  Optional<std::uint32_t> flags;
  // Sampling priority extracted from the incoming headers.
  Optional<int> sampling_priority;
};

}  // namespace tracing
}  // namespace datadog
