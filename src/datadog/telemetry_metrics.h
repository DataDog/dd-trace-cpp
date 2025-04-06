#pragma once

#include <datadog/telemetry/metrics.h>

namespace datadog::tracing::metrics {

// This structure contains all the metrics that are exposed by tracer
// telemetry.
namespace tracer {

/// TBD
extern const telemetry::Counter spans_created;
/// TBD
extern const telemetry::Counter spans_finished;
/// TBD
extern const telemetry::Counter trace_segments_created;
/// TBD
extern const telemetry::Counter trace_segments_closed;
/// TBD
extern const telemetry::Counter context_header_truncated;

namespace api {

/// TBD
extern const telemetry::Counter requests;
/// TBD
extern const telemetry::Counter responses;
/// TBD
extern const telemetry::Counter errors;

}  // namespace api
}  // namespace tracer

}  // namespace datadog::tracing::metrics
