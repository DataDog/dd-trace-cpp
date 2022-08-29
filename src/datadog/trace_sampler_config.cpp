#include "trace_sampler_config.h"
namespace datadog {
namespace tracing {

Expected<FinalizedTraceSamplerConfig> finalize_config(
    const TraceSamplerConfig &config) {
  FinalizedTraceSamplerConfig result;

  for (const auto &rule : config.rules) {
    auto maybe_rate = Rate::from(rule.sample_rate);
    if (auto *error = maybe_rate.if_error()) {
      std::string prefix;
      prefix +=
          "Unable to parse sample_rate in trace sampling rule with root span "
          "pattern ";
      prefix += rule.to_json();
      prefix += ": ";
      return error->with_prefix(prefix);
    }

    FinalizedTraceSamplerConfig::Rule finalized;
    static_cast<SpanMatcher &>(finalized) = rule;
    finalized.sample_rate = *maybe_rate;
    result.rules.push_back(std::move(finalized));
  }

  // If `sample_rate` was specified, then it translates to a "catch-all" rule
  // appended to the end of `rules`.  First, though, we have to make sure the
  // sample rate is valid.
  if (config.sample_rate) {
    auto maybe_rate = Rate::from(*config.sample_rate);
    if (auto *error = maybe_rate.if_error()) {
      return error->with_prefix(
          "Unable to parse overall sample_rate for trace sampling: ");
    }

    FinalizedTraceSamplerConfig::Rule catch_all;
    catch_all.sample_rate = *maybe_rate;
    result.rules.push_back(std::move(catch_all));
  }

  if (config.max_per_second <= 0) {
    std::string message;
    message +=
        "Trace sampling max_per_second must be greater than zero, but the "
        "following value was given: ";
    message += std::to_string(config.max_per_second);
    return Error{Error::MAX_PER_SECOND_OUT_OF_RANGE, std::move(message)};
  }
  result.max_per_second = config.max_per_second;

  return result;
}

}  // namespace tracing
}  // namespace datadog
