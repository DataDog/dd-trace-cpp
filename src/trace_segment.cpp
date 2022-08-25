#include "trace_segment.h"

#include <cassert>
#include <iostream>  // TODO: no
#include <optional>
#include <string>
#include <unordered_map>
#include <utility>

#include "collector.h"
#include "collector_response.h"
#include "dict_writer.h"
#include "error.h"
#include "span_data.h"
#include "tags.h"
#include "trace_sampler.h"

namespace datadog {
namespace tracing {
namespace {

class InjectionPolicy {
 public:
  virtual void trace_id(DictWriter&, std::uint64_t) = 0;
  virtual void parent_id(DictWriter&, std::uint64_t) = 0;
  virtual void sampling_priority(DictWriter&, int) = 0;
  virtual void origin(DictWriter&, const std::string&) = 0;
  virtual std::optional<Error> trace_tags(
      DictWriter&, const std::unordered_map<std::string, std::string>&) = 0;
};

class DatadogInjectionPolicy : public InjectionPolicy {
 public:
  void trace_id(DictWriter& writer, std::uint64_t trace_id) override {
    writer.set("x-datadog-trace-id", std::to_string(trace_id));
  }

  void parent_id(DictWriter& writer, std::uint64_t span_id) override {
    writer.set("x-datadog-parent-id", std::to_string(span_id));
  }

  void sampling_priority(DictWriter& writer, int sampling_priority) override {
    writer.set("x-datadog-sampling-priority",
               std::to_string(sampling_priority));
  }

  void origin(DictWriter& writer, const std::string& origin) override {
    writer.set("x-datadog-origin", origin);
  }

  std::optional<Error> trace_tags(
      DictWriter&,
      const std::unordered_map<std::string, std::string>& tags) override {
    if (tags.empty()) {
      return std::nullopt;
    }
    // TODO
    return Error{Error::NOT_IMPLEMENTED,
                 "Trace tags are not yet implemented, so I'm not going to "
                 "serialize them."};
  }
};

}  // namespace

TraceSegment::TraceSegment(
    const std::shared_ptr<Collector>& collector,
    const std::shared_ptr<TraceSampler>& trace_sampler,
    const std::shared_ptr<SpanSampler>& span_sampler,
    const std::shared_ptr<const SpanDefaults>& defaults,
    const PropagationStyles& injection_styles,
    const std::optional<std::string>& hostname,
    std::optional<std::string> origin,
    std::unordered_map<std::string, std::string> trace_tags,
    std::optional<SamplingDecision> sampling_decision,
    std::unique_ptr<SpanData> local_root)
    : collector_(collector),
      trace_sampler_(trace_sampler),
      span_sampler_(span_sampler),
      defaults_(defaults),
      injection_styles_(injection_styles),
      hostname_(hostname),
      origin_(std::move(origin)),
      trace_tags_(std::move(trace_tags)),
      num_finished_spans_(0),
      sampling_decision_(std::move(sampling_decision)) {
  assert(collector_);
  assert(trace_sampler_);
  assert(span_sampler_);
  assert(defaults_);

  register_span(std::move(local_root));
}

const SpanDefaults& TraceSegment::defaults() const { return *defaults_; }

const std::optional<std::string>& TraceSegment::hostname() const {
  return hostname_;
}

const std::optional<std::string>& TraceSegment::origin() const {
  return origin_;
}

std::optional<SamplingDecision> TraceSegment::sampling_decision() const {
  // `sampling_decision_` can change, so we need a lock.
  std::lock_guard<std::mutex> lock(mutex_);
  return sampling_decision_;
}

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
  spans_.front()->tags.insert(trace_tags_.begin(), trace_tags_.end());
  // TODO hack for now
  spans_.front()->numeric_tags["_sampling_priority_v1"] = 1;
  // end TODO

  const auto error = collector_->send(std::move(spans_), trace_sampler_);

  if (error) {
    // TODO: Looks like we do need a logger.
  }
}

void TraceSegment::make_sampling_decision_if_null() {
  // `mutex_` must already be locked.

  if (sampling_decision_) {
    return;
  }

  const SpanData& local_root = *spans_.front();
  sampling_decision_ = trace_sampler_->decide(
      local_root.trace_id, local_root.service, local_root.name,
      local_root.environment().value_or(""));

  // Update the decision maker trace tag.
  if (sampling_decision_->priority <= 0) {
    trace_tags_.erase(tags::internal::decision_maker);
  } else {
    trace_tags_[tags::internal::decision_maker] =
        "-" + std::to_string(*sampling_decision_->mechanism);
  }
}

void TraceSegment::inject(DictWriter& writer, const SpanData& span) {
  // TODO: I can assume this because of the current trace config validator.
  const PropagationStyles& styles = injection_styles_;
  assert(styles.datadog && !styles.b3 && !styles.w3c);
  (void)styles;
  // end TODO

  DatadogInjectionPolicy inject;

  inject.trace_id(writer, span.trace_id);
  inject.parent_id(writer, span.span_id);

  make_sampling_decision_if_null();
  inject.sampling_priority(writer, sampling_decision_->priority);

  if (origin_) {
    inject.origin(writer, *origin_);
  }

  // TODO: configurable maximum size
  if (const auto error = inject.trace_tags(writer, trace_tags_)) {
    // TODO: need a logger
    std::cout << *error << '\n';
    SpanData& local_root = *spans_.front();
    if (error->code == Error::TRACE_TAGS_EXCEED_MAXIMUM_LENGTH) {
      local_root.tags[tags::internal::propagation_error] = "inject_max_size";
    } else {
      local_root.tags[tags::internal::propagation_error] = "encoding_error";
    }
  }
}

}  // namespace tracing
}  // namespace datadog
