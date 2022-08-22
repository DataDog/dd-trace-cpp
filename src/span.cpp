#include "span.h"

#include <cassert>

#include "span_data.h"
#include "trace_segment.h"

namespace datadog {
namespace tracing {

Span::Span(SpanData* data, const std::shared_ptr<TraceSegment>& trace_segment,
           const std::function<std::uint64_t()>& generate_span_id,
           const Clock& clock)
    : data_(data),
      trace_segment_(trace_segment),
      generate_span_id_(generate_span_id),
      clock_(clock) {
  assert(data_);
  assert(trace_segment_);
  assert(generate_span_id_);
  assert(clock_);
}

Span Span::create_child(const SpanConfig& config) const {
  auto span_data =
      SpanData::with_config(trace_segment_->defaults(), config, clock_);
  span_data->trace_id = data_->trace_id;
  span_data->parent_id = data_->span_id;
  span_data->span_id = generate_span_id_();

  const auto span_data_ptr = span_data.get();
  trace_segment_->register_span(std::move(span_data));
  // TODO: Consider making `generate_span_id` a method of `TraceSegment`.
  return Span(span_data_ptr, trace_segment_, generate_span_id_, clock_);
}

TraceSegment& Span::trace_segment() { return *trace_segment_; }

const TraceSegment& Span::trace_segment() const { return *trace_segment_; }

}  // namespace tracing
}  // namespace datadog
