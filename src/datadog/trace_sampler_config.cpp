#include "trace_sampler_config.h"

#include <cmath>
#include <unordered_set>

#include "environment.h"
#include "json.hpp"
#include "parse_util.h"

namespace datadog {
namespace tracing {

TraceSamplerConfig::Rule::Rule(const SpanMatcher &base) : SpanMatcher(base) {}

Expected<FinalizedTraceSamplerConfig> finalize_config(
    const TraceSamplerConfig &config) {
  FinalizedTraceSamplerConfig result;

  std::vector<TraceSamplerConfig::Rule> rules = config.rules;

  for (const auto &rule : rules) {
    auto maybe_rate = Rate::from(rule.sample_rate);
    if (auto *error = maybe_rate.if_error()) {
      std::string prefix;
      prefix +=
          "Unable to parse sample_rate in trace sampling rule with root span "
          "pattern ";
      prefix += rule.to_json().dump();
      prefix += ": ";
      return error->with_prefix(prefix);
    }

    FinalizedTraceSamplerConfig::Rule finalized;
    static_cast<SpanMatcher &>(finalized) = rule;
    finalized.sample_rate = *maybe_rate;
    result.rules.push_back(std::move(finalized));
  }

  auto sample_rate = config.sample_rate;

  // If `sample_rate` was specified, then it translates to a "catch-all" rule
  // appended to the end of `rules`.  First, though, we have to make sure the
  // sample rate is valid.
  if (sample_rate) {
    auto maybe_rate = Rate::from(*sample_rate);
    if (auto *error = maybe_rate.if_error()) {
      return error->with_prefix(
          "Unable to parse overall sample_rate for trace sampling: ");
    }

    FinalizedTraceSamplerConfig::Rule catch_all;
    catch_all.sample_rate = *maybe_rate;
    result.rules.push_back(std::move(catch_all));
  }

  auto max_per_second = config.max_per_second.value_or(200);

  const auto allowed_types = {FP_NORMAL, FP_SUBNORMAL};
  if (!(max_per_second > 0) ||
      std::find(std::begin(allowed_types), std::end(allowed_types),
                std::fpclassify(max_per_second)) == std::end(allowed_types)) {
    std::string message;
    message +=
        "Trace sampling max_per_second must be greater than zero, but the "
        "following value was given: ";
    message += std::to_string(*config.max_per_second);
    return Error{Error::MAX_PER_SECOND_OUT_OF_RANGE, std::move(message)};
  }
  result.max_per_second = max_per_second;

  return result;
}

nlohmann::json to_json(const FinalizedTraceSamplerConfig::Rule &rule) {
  // Get the base class's fields, then add our own.
  auto result = static_cast<const SpanMatcher &>(rule).to_json();
  result["sample_rate"] = double(rule.sample_rate);
  return result;
}

}  // namespace tracing
}  // namespace datadog
