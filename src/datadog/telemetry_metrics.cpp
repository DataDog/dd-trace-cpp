#include "telemetry_metrics.h"

namespace datadog::tracing::metrics {

namespace tracer {
const telemetry::Counter spans_created = {"spans_created", "tracers", true};
const telemetry::Counter spans_finished = {"spans_finished", "tracers", true};

const telemetry::Counter trace_segments_created = {"trace_segments_created",
                                                   "tracers", true};

const telemetry::Counter trace_segments_closed = {"trace_segments_closed",
                                                  "tracers", true};
const telemetry::Counter context_header_truncated = {
    "context_header.truncated",
    "tracers",
    true,
};

namespace api {
const telemetry::Counter requests = {"trace_api.requests", "tracers", true};
const telemetry::Counter responses = {"trace_api.responses", "tracers", true};
const telemetry::Counter errors = {"trace_api.errors", "tracers", true};
}  // namespace api
}  // namespace tracer

}  // namespace datadog::tracing::metrics
