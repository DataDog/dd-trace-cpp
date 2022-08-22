#pragma once

#include <cstddef>
#include <memory>
#include <mutex>
#include <optional>
#include <vector>

#include "error.h"
#include "sampling_decision.h"

namespace datadog {
namespace tracing {

class Collector;
class DictReader;
class DictWriter;
class SpanData;
class SpanSampler;
class TraceSampler;

class TraceSegment {
  std::mutex mutex_;
  std::shared_ptr<Collector> collector_;
  std::shared_ptr<TraceSampler> trace_sampler_;
  std::shared_ptr<SpanSampler> span_sampler_;
  std::vector<std::unique_ptr<SpanData>> spans_;
  std::size_t num_finished_spans_;
  std::optional<SamplingDecision> sampling_decision_;

 public:
  TraceSegment(const std::shared_ptr<Collector>& collector,
               const std::shared_ptr<TraceSampler>& trace_sampler,
               const std::shared_ptr<SpanSampler>& span_sampler,
               const std::optional<SamplingDecision>& sampling_decision,
               std::unique_ptr<SpanData> local_root);

  // These are for sampling delegation, not for trace propagation.
  std::optional<Error> extract(const DictReader& reader);
  std::optional<Error> inject(DictWriter& writer) const;

  void register_span(std::unique_ptr<SpanData> span);
  void span_finished();

  // TODO: sampling-related stuff
};

}  // namespace tracing
}  // namespace datadog
