#include "trace_segment.h"

#include <cassert>
#include <charconv>
#include <limits>
#include <string>
#include <system_error>
#include <unordered_map>
#include <utility>

#include "collector.h"
#include "collector_response.h"
#include "dict_writer.h"
#include "error.h"
#include "logger.h"
#include "optional.h"
#include "span_data.h"
#include "span_sampler.h"
#include "tag_propagation.h"
#include "tags.h"
#include "trace_sampler.h"

namespace datadog {
namespace tracing {
namespace {

template <typename Integer>
std::string hex(Integer value) {
  // 4 bits per hex digit char, and then +1 char for possible minus sign
  char buffer[std::numeric_limits<Integer>::digits / 4 + 1];

  const int base = 16;
  auto result =
      std::to_chars(std::begin(buffer), std::end(buffer), value, base);
  assert(result.ec == std::errc());

  return std::string{std::begin(buffer), result.ptr};
}

}  // namespace

TraceSegment::TraceSegment(
    const std::shared_ptr<Logger>& logger,
    const std::shared_ptr<Collector>& collector,
    const std::shared_ptr<TraceSampler>& trace_sampler,
    const std::shared_ptr<SpanSampler>& span_sampler,
    const std::shared_ptr<const SpanDefaults>& defaults,
    const PropagationStyles& injection_styles,
    const Optional<std::string>& hostname, Optional<std::string> origin,
    std::size_t tags_header_max_size,
    std::unordered_map<std::string, std::string> trace_tags,
    Optional<SamplingDecision> sampling_decision,
    std::unique_ptr<SpanData> local_root)
    : logger_(logger),
      collector_(collector),
      trace_sampler_(trace_sampler),
      span_sampler_(span_sampler),
      defaults_(defaults),
      injection_styles_(injection_styles),
      hostname_(hostname),
      origin_(std::move(origin)),
      tags_header_max_size_(tags_header_max_size),
      trace_tags_(std::move(trace_tags)),
      num_finished_spans_(0),
      sampling_decision_(std::move(sampling_decision)) {
  assert(logger_);
  assert(collector_);
  assert(trace_sampler_);
  assert(span_sampler_);
  assert(defaults_);

  register_span(std::move(local_root));
}

const SpanDefaults& TraceSegment::defaults() const { return *defaults_; }

const Optional<std::string>& TraceSegment::hostname() const {
  return hostname_;
}

const Optional<std::string>& TraceSegment::origin() const { return origin_; }

Optional<SamplingDecision> TraceSegment::sampling_decision() const {
  // `sampling_decision_` can change, so we need a lock.
  std::lock_guard<std::mutex> lock(mutex_);
  return sampling_decision_;
}

Logger& TraceSegment::logger() const { return *logger_; }

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
  make_sampling_decision_if_null();
  assert(sampling_decision_);

  // All of our spans are finished.  Run the span sampler, finalize the spans,
  // and then send the spans to the collector.
  if (sampling_decision_->priority <= 0) {
    // Span sampling happens when the trace is dropped.
    for (const auto& span_ptr : spans_) {
      SpanData& span = *span_ptr;
      auto* rule = span_sampler_->match(span);
      if (!rule) {
        continue;
      }
      const SamplingDecision decision = rule->decide(span);
      if (decision.priority <= 0) {
        continue;
      }
      span.numeric_tags[tags::internal::span_sampling_mechanism] =
          *decision.mechanism;
      span.numeric_tags[tags::internal::span_sampling_rule_rate] =
          *decision.configured_rate;
      if (decision.limiter_max_per_second) {
        span.numeric_tags[tags::internal::span_sampling_limit] =
            *decision.limiter_max_per_second;
      }
    }
  }

  const SamplingDecision& decision = *sampling_decision_;

  auto& local_root = *spans_.front();
  local_root.tags.insert(trace_tags_.begin(), trace_tags_.end());
  local_root.numeric_tags[tags::internal::sampling_priority] =
      decision.priority;
  if (hostname_) {
    local_root.tags[tags::internal::hostname] = *hostname_;
  }
  if (decision.origin == SamplingDecision::Origin::LOCAL) {
    if (decision.mechanism == int(SamplingMechanism::AGENT_RATE) ||
        decision.mechanism == int(SamplingMechanism::DEFAULT)) {
      local_root.numeric_tags[tags::internal::agent_sample_rate] =
          *decision.configured_rate;
    } else if (decision.mechanism == int(SamplingMechanism::RULE)) {
      local_root.numeric_tags[tags::internal::rule_sample_rate] =
          *decision.configured_rate;
      if (decision.limiter_effective_rate) {
        local_root.numeric_tags[tags::internal::rule_limiter_sample_rate] =
            *decision.limiter_effective_rate;
      }
    }
  }

  // Origin is repeated on all spans.
  if (origin_) {
    for (const auto& span_ptr : spans_) {
      SpanData& span = *span_ptr;
      span.tags[tags::internal::origin] = *origin_;
    }
  }

  const auto result = collector_->send(std::move(spans_), trace_sampler_);
  if (auto* error = result.if_error()) {
    logger_->log_error(
        error->with_prefix("Error sending spans to collector: "));
  }
}

void TraceSegment::override_sampling_priority(int priority) {
  SamplingDecision decision;
  decision.priority = priority;
  decision.mechanism = int(SamplingMechanism::MANUAL);
  decision.origin = SamplingDecision::Origin::LOCAL;

  std::lock_guard<std::mutex> lock(mutex_);
  sampling_decision_ = decision;
  update_decision_maker_trace_tag();
}

void TraceSegment::make_sampling_decision_if_null() {
  // Depending on the context, `mutex_` might need already to be locked.

  if (sampling_decision_) {
    return;
  }

  const SpanData& local_root = *spans_.front();
  sampling_decision_ = trace_sampler_->decide(local_root);

  update_decision_maker_trace_tag();
}

void TraceSegment::update_decision_maker_trace_tag() {
  // Depending on the context, `mutex_` might need already to be locked.

  assert(sampling_decision_);

  if (sampling_decision_->priority <= 0) {
    trace_tags_.erase(tags::internal::decision_maker);
  } else {
    trace_tags_[tags::internal::decision_maker] =
        "-" + std::to_string(*sampling_decision_->mechanism);
  }
}

void TraceSegment::inject(DictWriter& writer, const SpanData& span) {
  int sampling_priority;
  std::string encoded_trace_tags;
  {
    std::lock_guard<std::mutex> lock(mutex_);
    make_sampling_decision_if_null();
    assert(sampling_decision_);
    sampling_priority = sampling_decision_->priority;
    encoded_trace_tags = encode_tags(trace_tags_);
  }

  // Origin and trace tag headers are always propagated, unless the only
  // injection style is "none".
  // Other headers depend on the injection styles.
  if (injection_styles_.none && !injection_styles_.datadog &&
      !injection_styles_.b3) {
    return;
  }

  if (origin_) {
    writer.set("x-datadog-origin", *origin_);
  }
  if (encoded_trace_tags.size() > tags_header_max_size_) {
    std::string message;
    message +=
        "Serialized x-datadog-tags header value is too large.  The configured "
        "maximum size is ";
    message += std::to_string(tags_header_max_size_);
    message += " bytes, but the encoded value is ";
    message += std::to_string(encoded_trace_tags.size());
    message += " bytes.";
    logger_->log_error(message);
    SpanData& local_root = *spans_.front();
    local_root.tags[tags::internal::propagation_error] = "inject_max_size";
  } else if (!encoded_trace_tags.empty()) {
    writer.set("x-datadog-tags", encoded_trace_tags);
  }

  if (injection_styles_.datadog) {
    writer.set("x-datadog-trace-id", std::to_string(span.trace_id));
    writer.set("x-datadog-parent-id", std::to_string(span.span_id));
    writer.set("x-datadog-sampling-priority",
               std::to_string(sampling_priority));
  }

  if (injection_styles_.b3) {
    writer.set("x-b3-traceid", hex(span.trace_id));
    writer.set("x-b3-spanid", hex(span.span_id));
    writer.set("x-b3-sampled", std::to_string(int(sampling_priority > 0)));
  }
}

}  // namespace tracing
}  // namespace datadog
