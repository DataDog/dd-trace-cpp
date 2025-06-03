#include "trace_sampler.h"

#include <datadog/sampling_decision.h>
#include <datadog/sampling_priority.h>

#include <algorithm>
#include <cassert>
#include <cstdint>
#include <limits>

#include "collector_response.h"
#include "json_serializer.h"
#include "sampling_util.h"
#include "span_data.h"
#include "tags.h"

namespace datadog {
namespace tracing {
namespace {

nlohmann::json to_json(const TraceSamplerRule& rule) {
  nlohmann::json j = rule.matcher;
  j["sample_rate"] = rule.rate.value();
  return j;
}

}  // namespace

TraceSampler::TraceSampler(const FinalizedTraceSamplerConfig& config,
                           const Clock& clock)
    : rules_(config.rules),
      limiter_(clock, config.max_per_second),
      limiter_max_per_second_(config.max_per_second) {}

void TraceSampler::set_rules(std::vector<TraceSamplerRule> rules) {
  std::lock_guard lock(mutex_);
  rules_ = std::move(rules);
}

SamplingDecision TraceSampler::decide(const SpanData& span) {
  SamplingDecision decision;
  decision.origin = SamplingDecision::Origin::LOCAL;

  // First check sampling rules.
  const auto found_rule =
      std::find_if(rules_.cbegin(), rules_.cend(),
                   [&](const auto& it) { return it.matcher.match(span); });

  // `mutex_` protects `limiter_`, `collector_sample_rates_`, and
  // `collector_default_sample_rate_`, so let's lock it here.
  std::lock_guard lock(mutex_);

  if (found_rule != rules_.end()) {
    const auto& rule = *found_rule;
    decision.mechanism = int(rule.mechanism);
    decision.limiter_max_per_second = limiter_max_per_second_;
    decision.configured_rate = rule.rate;
    const std::uint64_t threshold = max_id_from_rate(rule.rate);
    if (knuth_hash(span.trace_id.low) <= threshold) {
      const auto result = limiter_.allow();
      if (result.allowed) {
        decision.priority = int(SamplingPriority::USER_KEEP);
      } else {
        decision.priority = int(SamplingPriority::USER_DROP);
      }
      decision.limiter_effective_rate = result.effective_rate;
    } else {
      decision.priority = int(SamplingPriority::USER_DROP);
    }

    return decision;
  }

  // No sampling rule matched.  Find the appropriate collector-controlled
  // sample rate.
  auto found_rate = collector_sample_rates_.find(
      CollectorResponse::key(span.service, span.environment().value_or("")));
  if (found_rate != collector_sample_rates_.end()) {
    decision.configured_rate = found_rate->second;
    decision.mechanism = int(SamplingMechanism::AGENT_RATE);
  } else {
    if (collector_default_sample_rate_) {
      decision.configured_rate = *collector_default_sample_rate_;
      decision.mechanism = int(SamplingMechanism::AGENT_RATE);
    } else {
      // We have yet to receive a default rate from the collector.  This
      // corresponds to the `DEFAULT` sampling mechanism.
      decision.configured_rate = Rate::one();
      decision.mechanism = int(SamplingMechanism::DEFAULT);
    }
  }

  const std::uint64_t threshold = max_id_from_rate(*decision.configured_rate);
  if (knuth_hash(span.trace_id.low) <= threshold) {
    decision.priority = int(SamplingPriority::AUTO_KEEP);
  } else {
    decision.priority = int(SamplingPriority::AUTO_DROP);
  }

  return decision;
}

void TraceSampler::handle_collector_response(
    const CollectorResponse& response) {
  const auto found =
      response.sample_rate_by_key.find(response.key_of_default_rate);
  std::lock_guard<std::mutex> lock(mutex_);

  if (found != response.sample_rate_by_key.end()) {
    collector_default_sample_rate_ = found->second;
  }

  collector_sample_rates_ = response.sample_rate_by_key;
}

nlohmann::json TraceSampler::config_json() const {
  std::vector<nlohmann::json> rules;
  for (const auto& rule : rules_) {
    rules.push_back(to_json(rule));
  }

  return nlohmann::json::object({
      {"rules", rules},
      {"max_per_second", limiter_max_per_second_},
  });
}

SamplingDecision ApmDisabledTraceSampler::decide(const SpanData& span_data) {
  SamplingDecision decision;
  decision.origin = SamplingDecision::Origin::LOCAL;

  if (span_data.tags.find(tags::internal::trace_source) !=
      span_data.tags.end()) {
    decision.mechanism = static_cast<int>(SamplingMechanism::APP_SEC);
    decision.priority = static_cast<int>(SamplingPriority::USER_KEEP);
  } else {
    auto now = clock_();
    auto last_kept = last_kept_.load(std::memory_order_relaxed);
    auto num_asked = num_asked_.fetch_add(1, std::memory_order_relaxed) + 1;
    uint64_t num_allowed;
    if (now.wall - last_kept >= INTERVAL) {
      if (last_kept_.compare_exchange_strong(last_kept, now.wall)) {
        decision.priority = static_cast<int>(SamplingPriority::USER_KEEP);
        num_allowed = num_allowed_.fetch_add(1, std::memory_order_relaxed) + 1;
      } else {
        // another thread got to it first
        decision.priority = static_cast<int>(SamplingPriority::USER_DROP);
        num_allowed = num_allowed_.load(std::memory_order_relaxed);
      }
    } else {
      decision.priority = static_cast<int>(SamplingPriority::USER_DROP);
      num_allowed = num_allowed_.load(std::memory_order_relaxed);
    }

    decision.limiter_max_per_second = ALLOWED_PER_SECOND;
    double effective_rate = static_cast<double>(num_allowed) / num_asked;
    if (effective_rate > 1.0) {
      // can happen due to the relaxed atomic operations
      effective_rate = 1.0;
    }
    decision.limiter_effective_rate = Rate::from(effective_rate).value();
  }

  return decision;
}

void ApmDisabledTraceSampler::handle_collector_response(
    const CollectorResponse&) {
  // do nothing
}

nlohmann::json ApmDisabledTraceSampler::config_json() const {
  return nlohmann::json::object({
      {"max_per_second", ALLOWED_PER_SECOND},
  });
}

template <typename Ptr>
struct ErasedTraceSampler::Model : Concept {
  Model(Ptr&& samplerImpl) : impl_(std::move(samplerImpl)) {}

  SamplingDecision decide(const SpanData& span_data) override {
    return impl_->decide(span_data);
  }

  void handle_collector_response(const CollectorResponse& response) override {
    impl_->handle_collector_response(response);
  }

  nlohmann::json config_json() const override { return impl_->config_json(); }

 private:
  Ptr impl_;
};

template <typename Ptr>
ErasedTraceSampler::ErasedTraceSampler(Ptr samplerImpl) {
  impl_ = std::make_unique<Model<Ptr>>(std::move(samplerImpl));
}

template ErasedTraceSampler::ErasedTraceSampler(std::shared_ptr<TraceSampler>);
template ErasedTraceSampler::ErasedTraceSampler(
    std::unique_ptr<ApmDisabledTraceSampler>);

SamplingDecision ErasedTraceSampler::decide(const SpanData& span_data) {
  return impl_->decide(span_data);
}

void ErasedTraceSampler::handle_collector_response(
    const CollectorResponse& response) {
  impl_->handle_collector_response(response);
}

nlohmann::json ErasedTraceSampler::config_json() const {
  return impl_->config_json();
}

}  // namespace tracing
}  // namespace datadog
