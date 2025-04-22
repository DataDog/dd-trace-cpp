#pragma once

#include <datadog/telemetry/metrics.h>

namespace datadog::tracing::metrics {

namespace tracer {

/// The number of spans created by the tracer, tagged by manual API
/// (`integration_name:datadog`, `integration_name:otel` or
/// `integration_name:opentracing`).
extern const telemetry::Counter spans_created;

/// The number of spans finished, optionally (if implementation allows) tagged
/// manual API (`integration_name:datadog`, `integration_name:otel` or
/// `integration_name:opentracing`).
extern const telemetry::Counter spans_finished;

/// The number of trace segments (local traces) created, tagged with
/// new/continued depending on whether this is a new trace (no distributed
/// context information) or continued (has distributed context).
extern const telemetry::Counter trace_segments_created;

/// The number of trace segments (local traces) closed. In non partial flush
/// scenarios, trace_segments_closed == trace_chunks_enqueued.
extern const telemetry::Counter trace_segments_closed;

/// The number of times a context propagation header is truncated, tagged by the
/// reason for truncation (`truncation_reason:baggage_item_count_exceeded`,
/// `truncation_reason:baggage_byte_count_exceeded`).
extern const telemetry::Counter context_header_truncated;

namespace api {

/// The number of requests sent to the trace endpoint in the agent, regardless
/// of success.
extern const telemetry::Counter requests;

/// The number of responses received from the trace endpoint, tagged with status
/// code, e.g. `status_code:200`, `status_code:404`. May also use
/// `status_code:5xx` for example as a catch-all for 2xx, 3xx, 4xx, 5xx
/// responses.
extern const telemetry::Counter responses;

/// The number of requests sent to the trace endpoint in the agent that errored,
/// tagged by the error type (e.g. `type:timeout`, `type:network`,
/// `type:status_code`).
extern const telemetry::Counter errors;

}  // namespace api
}  // namespace tracer

}  // namespace datadog::tracing::metrics
