#include "telemetry_metrics.h"

#include <datadog/telemetry/telemetry.h>

namespace datadog::tracing::metrics {
namespace {

TracerMetrics make_tracer_metrics();

}  // namespace

// Never destroyed: without `telemetry::shutdown`, Telemetry sends its last
// metrics while static objects are destroyed, and the handles must still be
// there.
const TracerMetrics& tracer_metrics() {
  static const TracerMetrics* const metrics =
      new TracerMetrics(make_tracer_metrics());
  return *metrics;
}

namespace {

using namespace datadog::telemetry;

TracerMetrics make_tracer_metrics() {
  TracerMetrics metrics;
  metrics.spans_created = counter::create({"spans_created", "tracers", true},
                                          {"integration_name:datadog"});
  metrics.spans_dropped =
      counter::create({"spans_dropped", "tracers", true}, {"reason:p0_drop"});
  metrics.spans_finished = counter::create({"spans_finished", "tracers", true},
                                           {"integration_name:datadog"});
  metrics.trace_chunk_size =
      distribution::create({"trace_chunk_size", "tracers", true});
  metrics.trace_chunk_serialized_bytes = distribution::create(
      {"trace_chunk_serialization.bytes", "tracers", true});
  metrics.trace_chunk_serialization_duration =
      distribution::create({"trace_chunk_serialization.ms", "tracers", true});
  metrics.trace_chunks_enqueued =
      counter::create({"trace_chunks_enqueued", "tracers", true});
  metrics.trace_chunks_dropped = counter::create(
      {"trace_chunks_dropped", "tracers", true}, {"reason:p0_drop"});
  metrics.trace_chunks_sent =
      counter::create({"trace_chunks_sent", "tracers", true});
  metrics.trace_segments_created =
      counter::create<SegmentTags>({"trace_segments_created", "tracers", true});
  metrics.trace_segments_closed =
      counter::create({"trace_segments_closed", "tracers", true});

  metrics.api_requests =
      counter::create({"trace_api.requests", "tracers", true});
  metrics.api_responses =
      counter::create<StatusTags>({"trace_api.responses", "tracers", true});
  metrics.api_bytes_sent =
      distribution::create({"trace_api.bytes", "tracers", true});
  metrics.api_errors =
      counter::create({"trace_api.errors", "tracers", true}, {"type:network"});

  metrics.context_injected = counter::create<HeaderStyleTags>(
      {"context_header_style.injected", "tracers", true});
  metrics.context_extracted = counter::create<HeaderStyleTags>(
      {"context_header_style.extracted", "tracers", true});
  metrics.context_truncated = counter::create<TruncationTags>(
      {"context_header.truncated", "tracers", true});
  metrics.context_malformed =
      counter::create({"context_header_style.malformed", "tracers", true},
                      {"header_style:baggage"});
  return metrics;
}

}  // namespace
}  // namespace datadog::tracing::metrics
