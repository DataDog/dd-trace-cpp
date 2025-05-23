#include <datadog/collector.h>
#include <datadog/dict_reader.h>
#include <datadog/dict_writer.h>
#include <datadog/error.h>
#include <datadog/injection_options.h>
#include <datadog/logger.h>
#include <datadog/optional.h>
#include <datadog/span_defaults.h>
#include <datadog/telemetry/metrics.h>
#include <datadog/telemetry/telemetry.h>
#include <datadog/trace_segment.h>

#include <cassert>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include "config_manager.h"
#include "hex.h"
#include "platform_util.h"
#include "span_data.h"
#include "span_sampler.h"
#include "tag_propagation.h"
#include "tags.h"
#include "telemetry_metrics.h"
#include "trace_sampler.h"
#include "w3c_propagation.h"

namespace datadog {
namespace tracing {
namespace {

struct Cache {
  static int process_id;

  static void recalculate_values() { process_id = get_process_id(); }

  Cache() {
    recalculate_values();
    at_fork_in_child(&recalculate_values);
  }
};

int Cache::process_id;

// `cache_singleton` exists solely to invoke `Cache`'s constructor.
// All data members are static, so use e.g. `Cache::process_id` instead of
// `cache_singleton.process_id`.
Cache cache_singleton;

// Encode the specified `trace_tags`. If the encoded value is not longer than
// the specified `tags_header_max_size`, then set it as the "x-datadog-tags"
// header using the specified `writer`. If the encoded value is oversized, then
// write a diagnostic to the specified `logger` and set a propagation error tag
// on the specified `local_root_tags`.
void inject_trace_tags(
    DictWriter& writer,
    const std::vector<std::pair<std::string, std::string>>& trace_tags,
    std::size_t tags_header_max_size,
    std::unordered_map<std::string, std::string>& local_root_tags,
    Logger& logger) {
  const std::string encoded_trace_tags = encode_tags(trace_tags);

  if (encoded_trace_tags.size() > tags_header_max_size) {
    std::string message;
    message +=
        "Serialized x-datadog-tags header value is too large.  The configured "
        "maximum size is ";
    message += std::to_string(tags_header_max_size);
    message += " bytes, but the encoded value is ";
    message += std::to_string(encoded_trace_tags.size());
    message += " bytes.";
    logger.log_error(message);
    local_root_tags[tags::internal::propagation_error] = "inject_max_size";
  } else if (!encoded_trace_tags.empty()) {
    writer.set("x-datadog-tags", encoded_trace_tags);
  }
}

}  // namespace

TraceSegment::TraceSegment(
    const std::shared_ptr<Logger>& logger,
    const std::shared_ptr<Collector>& collector,
    const std::shared_ptr<TraceSampler>& trace_sampler,
    const std::shared_ptr<SpanSampler>& span_sampler,
    const std::shared_ptr<const SpanDefaults>& defaults,
    const std::shared_ptr<ConfigManager>& config_manager,
    const RuntimeID& runtime_id,
    const std::vector<PropagationStyle>& injection_styles,
    const Optional<std::string>& hostname, Optional<std::string> origin,
    std::size_t tags_header_max_size,
    std::vector<std::pair<std::string, std::string>> trace_tags,
    Optional<SamplingDecision> sampling_decision,
    Optional<std::string> additional_w3c_tracestate,
    Optional<std::string> additional_datadog_w3c_tracestate,
    std::unique_ptr<SpanData> local_root)
    : logger_(logger),
      collector_(collector),
      trace_sampler_(trace_sampler),
      span_sampler_(span_sampler),
      defaults_(defaults),
      runtime_id_(runtime_id),
      injection_styles_(injection_styles),
      hostname_(hostname),
      origin_(std::move(origin)),
      tags_header_max_size_(tags_header_max_size),
      trace_tags_(std::move(trace_tags)),
      num_finished_spans_(0),
      sampling_decision_(std::move(sampling_decision)),
      additional_w3c_tracestate_(std::move(additional_w3c_tracestate)),
      additional_datadog_w3c_tracestate_(
          std::move(additional_datadog_w3c_tracestate)),
      config_manager_(config_manager) {
  assert(logger_);
  assert(collector_);
  assert(trace_sampler_);
  assert(span_sampler_);
  assert(defaults_);
  assert(config_manager_);

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
  telemetry::counter::increment(metrics::tracer::spans_created,
                                {"integration_name:datadog"});

  std::lock_guard<std::mutex> lock(mutex_);
  assert(spans_.empty() || num_finished_spans_ < spans_.size());
  spans_.emplace_back(std::move(span));
}

void TraceSegment::span_finished() {
  {
    telemetry::counter::increment(metrics::tracer::spans_finished,
                                  {"integration_name:datadog"});
    std::lock_guard<std::mutex> lock(mutex_);
    ++num_finished_spans_;
    assert(num_finished_spans_ <= spans_.size());
    if (num_finished_spans_ < spans_.size()) {
      return;
    }
  }

  telemetry::counter::increment(metrics::tracer::trace_chunks_enqueued);

  // We don't need the lock anymore.  There's nobody left to call our methods.
  // On the other hand, there's nobody left to contend for the mutex, so it
  // doesn't make any difference.
  make_sampling_decision_if_null();
  assert(sampling_decision_);

  // All of our spans are finished. Run the span sampler, finalize the spans,
  // and then send the spans to the collector.
  if (sampling_decision_->priority <= 0) {
    telemetry::counter::increment(metrics::tracer::trace_chunks_dropped,
                                  {"reason:p0_drop"});
    // Span sampling happens when the trace is dropped.
    for (const auto& span_ptr : spans_) {
      SpanData& span = *span_ptr;
      auto* rule = span_sampler_->match(span);
      if (!rule) {
        continue;
      }
      const SamplingDecision decision = rule->decide(span);
      if (decision.priority <= 0) {
        telemetry::counter::increment(metrics::tracer::spans_dropped,
                                      {"reason:p0_drop"});
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
    } else if (decision.mechanism == int(SamplingMechanism::RULE) ||
               decision.mechanism == int(SamplingMechanism::REMOTE_RULE) ||
               decision.mechanism ==
                   int(SamplingMechanism::REMOTE_ADAPTIVE_RULE)) {
      local_root.numeric_tags[tags::internal::rule_sample_rate] =
          *decision.configured_rate;
      if (decision.limiter_effective_rate) {
        local_root.numeric_tags[tags::internal::rule_limiter_sample_rate] =
            *decision.limiter_effective_rate;
      }
    }
  }
  if (decision.origin == SamplingDecision::Origin::DELEGATED &&
      local_root.parent_id == 0) {
    // Convey the fact that, even though we are the root service, we delegated
    // the sampling decision and so are not the "sampling decider."
    local_root.tags[tags::internal::sampling_decider] = "0";
  }

  // Some tags are repeated on all spans.
  for (const auto& span_ptr : spans_) {
    SpanData& span = *span_ptr;
    if (origin_) {
      span.tags[tags::internal::origin] = *origin_;
    }
    span.numeric_tags[tags::internal::process_id] = Cache::process_id;
    span.tags[tags::internal::language] = "cpp";
    span.tags[tags::internal::runtime_id] = runtime_id_.string();
  }

  if (config_manager_->report_traces()) {
    telemetry::distribution::add(metrics::tracer::trace_chunk_size,
                                 spans_.size());

    telemetry::counter::increment(metrics::tracer::trace_chunks_sent);
    const auto result = collector_->send(std::move(spans_), trace_sampler_);
    if (auto* error = result.if_error()) {
      logger_->log_error(
          error->with_prefix("Error sending spans to collector: "));
    }
  }

  telemetry::counter::increment(metrics::tracer::trace_segments_closed);
}

void TraceSegment::override_sampling_priority(SamplingPriority priority) {
  override_sampling_priority(static_cast<int>(priority));
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

  // Note that `found` might be erased below (in case you refactor this code).
  const auto found = std::find_if(
      trace_tags_.begin(), trace_tags_.end(), [](const auto& entry) {
        return entry.first == tags::internal::decision_maker;
      });

  if (sampling_decision_->priority <= 0) {
    if (found != trace_tags_.end()) {
      trace_tags_.erase(found);
    }
    return;
  }

  // Note that `value` is moved-from below (in case you refactor this code).
  auto value = "-" + std::to_string(*sampling_decision_->mechanism);
  if (found == trace_tags_.end()) {
    trace_tags_.emplace_back(tags::internal::decision_maker, std::move(value));
  } else {
    found->second = std::move(value);
  }
}

bool TraceSegment::inject(DictWriter& writer, const SpanData& span) {
  return inject(writer, span, InjectionOptions{});
}

bool TraceSegment::inject(DictWriter& writer, const SpanData& span,
                          const InjectionOptions&) {
  // If the only injection style is `NONE`, then don't do anything.
  if (injection_styles_.size() == 1 &&
      injection_styles_[0] == PropagationStyle::NONE) {
    return false;
  }

  // The sampling priority can change (it can be overridden on another thread),
  // and trace tags might change when that happens ("_dd.p.dm").
  // So, we lock here, make a sampling decision if necessary, and then copy the
  // decision and trace tags before unlocking.
  int sampling_priority;
  std::vector<std::pair<std::string, std::string>> trace_tags;
  {
    std::lock_guard<std::mutex> lock(mutex_);
    make_sampling_decision_if_null();
    assert(sampling_decision_);
    sampling_priority = sampling_decision_->priority;
    trace_tags = trace_tags_;
  }

  for (const auto style : injection_styles_) {
    switch (style) {
      case PropagationStyle::DATADOG:
        writer.set("x-datadog-trace-id", std::to_string(span.trace_id.low));
        writer.set("x-datadog-parent-id", std::to_string(span.span_id));
        writer.set("x-datadog-sampling-priority",
                   std::to_string(sampling_priority));
        if (origin_) {
          writer.set("x-datadog-origin", *origin_);
        }
        inject_trace_tags(writer, trace_tags, tags_header_max_size_,
                          spans_.front()->tags, *logger_);

        telemetry::counter::increment(metrics::tracer::trace_context::injected,
                                      {"header_style:datadog"});
        break;
      case PropagationStyle::B3:
        if (span.trace_id.high) {
          writer.set("x-b3-traceid", span.trace_id.hex_padded());
        } else {
          writer.set("x-b3-traceid", hex_padded(span.trace_id.low));
        }
        writer.set("x-b3-spanid", hex_padded(span.span_id));
        writer.set("x-b3-sampled", std::to_string(int(sampling_priority > 0)));
        if (origin_) {
          writer.set("x-datadog-origin", *origin_);
        }
        inject_trace_tags(writer, trace_tags, tags_header_max_size_,
                          spans_.front()->tags, *logger_);
        telemetry::counter::increment(metrics::tracer::trace_context::injected,
                                      {"header_style:b3multi"});
        break;
      case PropagationStyle::W3C:
        writer.set(
            "traceparent",
            encode_traceparent(span.trace_id, span.span_id, sampling_priority));
        writer.set(
            "tracestate",
            encode_tracestate(span.span_id, sampling_priority, origin_,
                              trace_tags, additional_datadog_w3c_tracestate_,
                              additional_w3c_tracestate_));
        telemetry::counter::increment(metrics::tracer::trace_context::injected,
                                      {"header_style:tracecontext"});
        break;
      default:
        break;
    }
  }

  return true;
}

}  // namespace tracing
}  // namespace datadog
