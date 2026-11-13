#pragma once

#include <datadog/telemetry/metric_handle.h>

#include <array>
#include <memory>
#include <tuple>

#include "telemetry/status_tags.h"

namespace datadog::tracing::metrics {

// Whether a trace segment starts a new trace or continues a distributed one.
enum class Continuation { new_trace, continued };

constexpr std::array<const char*, 2> tag_value_names(Continuation) {
  return {"new", "continued"};
}

struct SegmentTags {
  Continuation new_continued = Continuation::new_trace;

  static auto tag_fields() {
    return std::tuple{
        telemetry::tag("new_continued", &SegmentTags::new_continued)};
  }
};

// The style of a trace context header.
enum class HeaderStyle { datadog, b3multi, tracecontext, baggage, none };

constexpr std::array<const char*, 5> tag_value_names(HeaderStyle) {
  return {"datadog", "b3multi", "tracecontext", "baggage", "none"};
}

struct HeaderStyleTags {
  HeaderStyle header_style = HeaderStyle::datadog;

  static auto tag_fields() {
    return std::tuple{
        telemetry::tag("header_style", &HeaderStyleTags::header_style)};
  }
};

// Why a trace context header was truncated.
enum class TruncationReason {
  baggage_item_count_exceeded,
  baggage_byte_count_exceeded
};

constexpr std::array<const char*, 2> tag_value_names(TruncationReason) {
  return {"baggage_item_count_exceeded", "baggage_byte_count_exceeded"};
}

struct TruncationTags {
  TruncationReason truncation_reason =
      TruncationReason::baggage_item_count_exceeded;

  static auto tag_fields() {
    return std::tuple{telemetry::tag("truncation_reason",
                                     &TruncationTags::truncation_reason)};
  }
};

// The metrics of the tracer. Every tracer of a process shares them.
struct TracerMetrics {
  /// The number of spans created by the tracer, tagged by manual API
  /// (`integration_name:datadog`).
  std::shared_ptr<telemetry::CounterHandle<>> spans_created;
  /// The number of spans dropped and the reason for being dropped
  /// (`reason:p0_drop`: the span was part of a p0 trace that was dropped by
  /// the tracer).
  std::shared_ptr<telemetry::CounterHandle<>> spans_dropped;
  /// The number of spans finished, tagged by manual API
  /// (`integration_name:datadog`).
  std::shared_ptr<telemetry::CounterHandle<>> spans_finished;
  /// The number of spans in the trace chunk when it is enqueued.
  std::shared_ptr<telemetry::DistributionHandle<>> trace_chunk_size;
  /// The size in bytes of the serialized trace chunk.
  std::shared_ptr<telemetry::DistributionHandle<>> trace_chunk_serialized_bytes;
  /// The time it takes to serialize a trace chunk.
  std::shared_ptr<telemetry::DistributionHandle<>>
      trace_chunk_serialization_duration;
  /// The number of times a trace chunk is enqueued for sampling/serialization.
  /// In partial-flush scenarios, multiple trace chunks may be enqueued per
  /// trace segment/local trace.
  std::shared_ptr<telemetry::CounterHandle<>> trace_chunks_enqueued;
  /// The number of trace chunks dropped prior to serialization, tagged by
  /// reason (`reason:p0_drop`: the trace was a p0 trace that was dropped by
  /// the tracer).
  std::shared_ptr<telemetry::CounterHandle<>> trace_chunks_dropped;
  /// The number of trace chunks attempted to be sent to the backend,
  /// regardless of response.
  std::shared_ptr<telemetry::CounterHandle<>> trace_chunks_sent;
  /// The number of trace segments (local traces) created, tagged with
  /// new/continued depending on whether this is a new trace (no distributed
  /// context information) or continued (has distributed context).
  std::shared_ptr<telemetry::CounterHandle<SegmentTags>> trace_segments_created;
  /// The number of trace segments (local traces) closed. In non partial flush
  /// scenarios, trace_segments_closed == trace_chunks_enqueued.
  std::shared_ptr<telemetry::CounterHandle<>> trace_segments_closed;

  /// The number of requests sent to the trace endpoint in the agent,
  /// regardless of success.
  std::shared_ptr<telemetry::CounterHandle<>> api_requests;
  /// The number of responses received from the trace endpoint, tagged with
  /// the status code class (`status_code:2xx` and so on).
  std::shared_ptr<telemetry::CounterHandle<telemetry::StatusTags>>
      api_responses;
  /// The size of the payload sent to the endpoint in bytes.
  std::shared_ptr<telemetry::DistributionHandle<>> api_bytes_sent;
  /// The number of requests sent to the trace endpoint in the agent that
  /// errored, tagged by the error type (`type:network`).
  std::shared_ptr<telemetry::CounterHandle<>> api_errors;

  /// The number of times distributed context is injected into an outgoing
  /// span, tagged by header style.
  std::shared_ptr<telemetry::CounterHandle<HeaderStyleTags>> context_injected;
  /// The number of times distributed context is successfully extracted from
  /// an incoming request, tagged by header style.
  std::shared_ptr<telemetry::CounterHandle<HeaderStyleTags>> context_extracted;
  /// The number of times a context propagation header is truncated, tagged by
  /// the reason for truncation.
  std::shared_ptr<telemetry::CounterHandle<TruncationTags>> context_truncated;
  /// The number of times baggage headers are dropped because they're
  /// malformed (missing key/value/'='), tagged by header style
  /// (`header_style:baggage`).
  std::shared_ptr<telemetry::CounterHandle<>> context_malformed;
};

// The metrics of the tracer, created on first use. Call it only after
// `telemetry::init`: before, the metrics would never be sent.
const TracerMetrics& tracer_metrics();

}  // namespace datadog::tracing::metrics
