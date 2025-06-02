#pragma once

namespace datadog {
namespace tracing {

// Based on the OTel Trace API
// https://opentelemetry.io/docs/specs/otel/trace/api
#define OTEL_TRACE_ID_IDENTIFIER "TraceId"
#define OTEL_SPAN_ID_IDENTIFIER "SpanId"

}  // namespace tracing
}  // namespace datadog
