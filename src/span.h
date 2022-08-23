#pragma once

#include <functional>
#include <memory>
#include <optional>
#include <string_view>

#include "clock.h"
#include "error.h"
#include "validated.h"

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

 public:
  Span(SpanData* data, const std::shared_ptr<TraceSegment>& trace_segment,
       const std::function<std::uint64_t()>& generate_span_id,
       const Clock& clock);
  Span(const Span&) = delete;
  Span(Span&&) = default;

  ~Span();

  Span create_child(const SpanConfig& config) const;

  std::optional<std::string_view> lookup_tag(std::string_view name) const;
  void set_tag(std::string_view name, std::string_view value);
  void remove_tag(std::string_view name);

  std::optional<Error> inject(DictWriter& writer) const;

  TraceSegment& trace_segment();
  const TraceSegment& trace_segment() const;
};

}  // namespace tracing
}  // namespace datadog
