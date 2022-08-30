#pragma once

#include <chrono>
#include <functional>
#include <memory>
#include <optional>
#include <string_view>

#include "clock.h"
#include "error.h"

namespace datadog {
namespace tracing {

class DictWriter;
class SpanConfig;
class SpanData;
class TraceSegment;

class Span {
  std::shared_ptr<TraceSegment> trace_segment_;
  SpanData* data_;
  std::function<std::uint64_t()> generate_span_id_;
  Clock clock_;
  std::optional<std::chrono::steady_clock::time_point> end_time_;

 public:
  Span(SpanData* data, const std::shared_ptr<TraceSegment>& trace_segment,
       const std::function<std::uint64_t()>& generate_span_id,
       const Clock& clock);
  Span(const Span&) = delete;
  Span(Span&&) = default;
  Span& operator=(Span&&) = default;

  ~Span();

  Span create_child(const SpanConfig& config) const;

  std::uint64_t id() const;
  std::uint64_t trace_id() const;

  std::optional<std::string_view> lookup_tag(std::string_view name) const;
  void set_tag(std::string_view name, std::string_view value);
  void remove_tag(std::string_view name);

  void set_service_name(std::string_view);
  void set_service_type(std::string_view);
  void set_operation_name(std::string_view);
  void set_resource_name(std::string_view);
  void set_error(std::string_view);
  void set_error(bool);
  void set_end_time(std::chrono::steady_clock::time_point);

  void inject(DictWriter& writer) const;

  TraceSegment& trace_segment();
  const TraceSegment& trace_segment() const;
};

}  // namespace tracing
}  // namespace datadog
