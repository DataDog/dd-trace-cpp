#include "span.h"

#include <cassert>
#include <optional>
#include <string>
#include <string_view>

#include "dict_writer.h"
#include "span_config.h"
#include "span_data.h"
#include "tags.h"
#include "trace_segment.h"

namespace datadog {
namespace tracing {

Span::Span(SpanData* data, const std::shared_ptr<TraceSegment>& trace_segment,
           const IDGenerator& generate_span_id, const Clock& clock)
    : trace_segment_(trace_segment),
      data_(data),
      generate_span_id_(generate_span_id),
      clock_(clock) {
  assert(trace_segment_);
  assert(data_);
  assert(generate_span_id_);
  assert(clock_);
}

Span::~Span() {
  if (!trace_segment_) {
    // We were moved from.
    return;
  }

  if (end_time_) {
    data_->duration = *end_time_ - data_->start.tick;
  } else {
    const auto now = clock_();
    data_->duration = now - data_->start;
  }

  trace_segment_->span_finished();
}

Span Span::create_child(const SpanConfig& config) const {
  auto span_data = std::make_unique<SpanData>();
  span_data->apply_config(trace_segment_->defaults(), config, clock_);
  span_data->trace_id = data_->trace_id;
  span_data->parent_id = data_->span_id;
  span_data->span_id = generate_span_id_();

  const auto span_data_ptr = span_data.get();
  trace_segment_->register_span(std::move(span_data));
  return Span(span_data_ptr, trace_segment_, generate_span_id_, clock_);
}

Span Span::create_child() const { return create_child(SpanConfig{}); }

void Span::inject(DictWriter& writer) const {
  trace_segment_->inject(writer, *data_);
}

std::uint64_t Span::id() const { return data_->span_id; }

std::uint64_t Span::trace_id() const { return data_->trace_id; }

std::optional<std::uint64_t> Span::parent_id() const {
  if (data_->parent_id == 0) {
    return std::nullopt;
  }
  return data_->parent_id;
}

TimePoint Span::start_time() const { return data_->start; }

bool Span::error() const { return data_->error; }

std::optional<std::string_view> Span::lookup_tag(std::string_view name) const {
  if (tags::is_internal(name)) {
    return std::nullopt;
  }

  const auto found = data_->tags.find(std::string(name));
  if (found == data_->tags.end()) {
    return std::nullopt;
  }
  return found->second;
}

void Span::set_tag(std::string_view name, std::string_view value) {
  if (!tags::is_internal(name)) {
    data_->tags.insert_or_assign(std::string(name), std::string(value));
  }
}

void Span::remove_tag(std::string_view name) {
  if (!tags::is_internal(name)) {
    data_->tags.erase(std::string(name));
  }
}

void Span::set_service_name(std::string_view service) {
  data_->service = service;
}

void Span::set_service_type(std::string_view type) {
  data_->service_type = type;
}

void Span::set_resource_name(std::string_view resource) {
  data_->resource = resource;
}

void Span::set_error(bool is_error) {
  data_->error = is_error;
  if (!is_error) {
    data_->tags.erase("error.msg");
    data_->tags.erase("error.type");
  }
}

void Span::set_error_message(std::string_view message) {
  data_->error = true;
  data_->tags.insert_or_assign("error.msg", std::string(message));
}

void Span::set_error_type(std::string_view type) {
  data_->error = true;
  data_->tags.insert_or_assign("error.type", std::string(type));
}

void Span::set_error_stack(std::string_view type) {
  data_->error = true;
  data_->tags.insert_or_assign("error.stack", std::string(type));
}

void Span::set_name(std::string_view value) { data_->name = value; }

void Span::set_end_time(std::chrono::steady_clock::time_point end_time) {
  end_time_ = end_time;
}

TraceSegment& Span::trace_segment() { return *trace_segment_; }

const TraceSegment& Span::trace_segment() const { return *trace_segment_; }

}  // namespace tracing
}  // namespace datadog
