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
class SpanDefaults;
class SpanSampler;
class TraceSampler;

class TraceSegment {
  mutable std::mutex mutex_;
  std::shared_ptr<Collector> collector_;
  std::shared_ptr<TraceSampler> trace_sampler_;
  std::shared_ptr<SpanSampler> span_sampler_;
  std::shared_ptr<const SpanDefaults> defaults_;
  std::vector<std::unique_ptr<SpanData>> spans_;
  std::size_t num_finished_spans_;
  std::optional<SamplingDecision> sampling_decision_;

 public:
  TraceSegment(const std::shared_ptr<Collector>& collector,
               const std::shared_ptr<TraceSampler>& trace_sampler,
               const std::shared_ptr<SpanSampler>& span_sampler,
               const std::shared_ptr<const SpanDefaults>& defaults,
               const std::optional<SamplingDecision>& sampling_decision,
               std::unique_ptr<SpanData> local_root);

  const SpanDefaults& defaults() const;

  // These are for sampling delegation, not for trace propagation.
  std::optional<Error> extract(const DictReader& reader);
  std::optional<Error> inject(DictWriter& writer) const;

  void register_span(std::unique_ptr<SpanData> span);
  void span_finished();

  // TODO: sampling-related stuff

  // TODO: This might be nice for testing.
  template <typename Visitor>
  void visit_spans(Visitor&& visitor) const;
};

template <typename Visitor>
void TraceSegment::visit_spans(Visitor&& visitor) const {
  std::lock_guard<std::mutex> lock(mutex_);
  visitor(spans_);
}

}  // namespace tracing
}  // namespace datadog
