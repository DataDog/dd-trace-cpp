#include "trace_segment.h"

#include <cassert>

#include "collector.h"
#include "collector_response.h"
#include "span_data.h"
#include "trace_sampler.h"

namespace datadog {
namespace tracing {

TraceSegment::TraceSegment(
    const std::shared_ptr<Collector>& collector,
    const std::shared_ptr<TraceSampler>& trace_sampler,
    const std::shared_ptr<SpanSampler>& span_sampler,
    const std::shared_ptr<const SpanDefaults>& defaults,
    const std::optional<SamplingDecision>& sampling_decision,
    std::unique_ptr<SpanData> local_root)
    : collector_(collector),
      trace_sampler_(trace_sampler),
      span_sampler_(span_sampler),
      defaults_(defaults),
      num_finished_spans_(0),
      sampling_decision_(sampling_decision) {
  assert(collector_);
  assert(trace_sampler_);
  assert(span_sampler_);
  assert(defaults_);

  register_span(std::move(local_root));
}

const SpanDefaults& TraceSegment::defaults() const { return *defaults_; }

void TraceSegment::register_span(std::unique_ptr<SpanData> span) {
  std::lock_guard<std::mutex> lock(mutex_);
  assert(spans_.empty() || num_finished_spans_ < spans_.size());
  spans_.emplace_back(std::move(span));
}

void TraceSegment::span_finished() {
  {
    std::lock_guard<std::mutex> lock(mutex_);
    ++num_finished_spans_;
    assert(num_finished_spans_ <= spans_.size());
    if (num_finished_spans_ < spans_.size()) {
      return;
    }
  }
  // We don't need the lock anymore.  There's nobody left to call our methods.
  // On the other hand, there's nobody left to contend for the mutex, so it
  // doesn't make any difference.

  // All of our spans are finished.  Run the span sampler, finalize the spans,
  // and then send the spans to the collector.
  // TODO: span sampler
  // TODO: Finalize the spans, e.g. add special tags to local root span.

  const auto error = collector_->send(
      std::move(spans_),
      [trace_sampler_ = trace_sampler_](const CollectorResponse& response) {
        trace_sampler_->handle_collector_response(response);
      });

  if (error) {
    // TODO: Looks like we do need a logger.
  }
}

}  // namespace tracing
}  // namespace datadog
