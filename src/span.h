#pragma once

#include <memory>
#include <optional>
#include <string_view>

#include "error.h"

namespace datadog {
namespace tracing {

class DictWriter;
class SpanConfig;
class SpanData;
class TraceSegment;

class Span {
  SpanData* data_;
  std::shared_ptr<TraceSegment> trace_segment_;

 public:
  Span(SpanData* data, const std::shared_ptr<TraceSegment>& trace_segment);

  void finish();
  // TODO: clocks

  std::optional<std::string_view> lookup_tag(std::string_view name) const;
  void set_tag(std::string_view name, std::string_view value);

  std::optional<Error> inject(DictWriter& writer) const;

  TraceSegment& trace_segment();
  const TraceSegment& trace_segment() const;
};

}  // namespace tracing
}  // namespace datadog
