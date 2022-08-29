#include "span_sampler_config.h"

namespace datadog {
namespace tracing {

Expected<FinalizedSpanSamplerConfig> finalize_config(
    const SpanSamplerConfig &config) {
  FinalizedSpanSamplerConfig result;

  for (const auto &rule : config.rules) {
    auto maybe_rate = Rate::from(rule.sample_rate);
    if (auto *error = maybe_rate.if_error()) {
      std::string prefix;
      prefix +=
          "Unable to parse sample_rate in span sampling rule with span "
          "pattern ";
      prefix += rule.to_json();
      prefix += ": ";
      return error->with_prefix(prefix);
    }

    if (rule.max_per_second && !(*rule.max_per_second > 0)) {
      std::string message;
      message += "Span sampling rule with pattern ";
      message += rule.to_json();
      message +=
          " should have a max_per_second value be greater than zero, but the "
          "following value was given: ";
      message += std::to_string(*rule.max_per_second);
      return Error{Error::MAX_PER_SECOND_OUT_OF_RANGE, std::move(message)};
    }

    FinalizedSpanSamplerConfig::Rule finalized;
    static_cast<SpanMatcher &>(finalized) = rule;
    finalized.sample_rate = *maybe_rate;
    finalized.max_per_second = rule.max_per_second;
    result.rules.push_back(std::move(finalized));
  }

  return result;
}

}  // namespace tracing
}  // namespace datadog
