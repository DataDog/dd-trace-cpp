#include "trace_segment.h"

#include "span_data.h"

namespace datadog {
namespace tracing {

TraceSegment::TraceSegment(
    const std::shared_ptr<Collector>& collector,
    const std::shared_ptr<TraceSampler>& trace_sampler,
    const std::shared_ptr<SpanSampler>& span_sampler,
    const std::optional<SamplingDecision>& sampling_decision,
    std::unique_ptr<SpanData> local_root)
    : collector_(collector),
      trace_sampler_(trace_sampler),
      span_sampler_(span_sampler),
      num_finished_spans_(0),
      sampling_decision_(sampling_decision) {
  register_span(std::move(local_root));
}

void TraceSegment::register_span(std::unique_ptr<SpanData> span) {
  std::lock_guard<std::mutex> lock(mutex_);
  spans_.emplace_back(std::move(span));
}

void TraceSegment::span_finished() {
  std::lock_guard<std::mutex> lock(mutex_);
  ++num_finished_spans_;
  // TODO
}

}  // namespace tracing
}  // namespace datadog
