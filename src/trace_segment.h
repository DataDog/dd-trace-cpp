#pragma once

#include <memory>
#include <optional>
#include <vector>

#include "error.h"

namespace datadog {
namespace tracing {

class Collector;
class DictReader;
class DictWriter;
class SpanData;
class SpanSampler;
class TraceSampler;

class TraceSegment {
  std::shared_ptr<Collector> collector_;
  std::shared_ptr<TraceSampler> trace_sampler_;
  std::shared_ptr<SpanSampler> span_sampler_;
  std::vector<std::unique_ptr<SpanData>> spans_;

 public:
  TraceSegment(const std::shared_ptr<Collector>& collector,
               const std::shared_ptr<TraceSampler>& trace_sampler,
               const std::shared_ptr<SpanSampler>& span_sampler);

  // These are for sampling delegation, not for trace propagation.
  std::optional<Error> extract(const DictReader& reader);
  std::optional<Error> inject(DictWriter& writer) const;

  void register_span(std::unique_ptr<SpanData> span);
  void span_finished();

  // TODO: sampling-related stuff
};

}  // namespace tracing
}  // namespace datadog
