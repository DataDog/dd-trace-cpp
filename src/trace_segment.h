#pragma once

#include <cstddef>
#include <memory>
#include <mutex>
#include <optional>
#include <unordered_map>
#include <vector>

#include "expected.h"
#include "propagation_styles.h"
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
  const PropagationStyles injection_styles_;
  const std::optional<std::string> hostname_;
  const std::optional<std::string> origin_;
  std::unordered_map<std::string, std::string> trace_tags_;

  std::vector<std::unique_ptr<SpanData>> spans_;
  std::size_t num_finished_spans_;
  std::optional<SamplingDecision> sampling_decision_;
  bool awaiting_delegated_sampling_decision_ = false;

 public:
  TraceSegment(const std::shared_ptr<Collector>& collector,
               const std::shared_ptr<TraceSampler>& trace_sampler,
               const std::shared_ptr<SpanSampler>& span_sampler,
               const std::shared_ptr<const SpanDefaults>& defaults,
               const PropagationStyles& injection_styles,
               const std::optional<std::string>& hostname,
               std::optional<std::string> origin,
               std::unordered_map<std::string, std::string> trace_tags,
               std::optional<SamplingDecision> sampling_decision,
               std::unique_ptr<SpanData> local_root);

  const SpanDefaults& defaults() const;
  const std::optional<std::string>& hostname() const;
  const std::optional<std::string>& origin() const;
  std::optional<SamplingDecision> sampling_decision() const;

  // This is for trace propagation.
  void inject(DictWriter&, const SpanData&);

  // These are for sampling delegation, not for trace propagation.
  // TODO
  Expected<void> extract(const DictReader& reader);
  void inject(DictWriter& writer) const;

  void register_span(std::unique_ptr<SpanData> span);
  void span_finished();

  // TODO: sampling-related stuff

  // TODO: This might be nice for testing.
  template <typename Visitor>
  void visit_spans(Visitor&& visitor) const;

 private:
  void make_sampling_decision_if_null();
};

template <typename Visitor>
void TraceSegment::visit_spans(Visitor&& visitor) const {
  std::lock_guard<std::mutex> lock(mutex_);
  visitor(spans_);
}

}  // namespace tracing
}  // namespace datadog
