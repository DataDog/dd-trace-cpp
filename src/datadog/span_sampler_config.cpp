#include "span_sampler_config.h"

#include <cmath>
#include <sstream>
#include <unordered_set>

#include "environment.h"
#include "expected.h"
#include "json.hpp"

namespace datadog {
namespace tracing {

SpanSamplerConfig::Rule::Rule(const SpanMatcher &base) : SpanMatcher(base) {}

Expected<FinalizedSpanSamplerConfig> finalize_config(
    const SpanSamplerConfig &config) {
  FinalizedSpanSamplerConfig result;

  std::vector<SpanSamplerConfig::Rule> rules = config.rules;

  for (const auto &rule : rules) {
    auto maybe_rate = Rate::from(rule.sample_rate);
    if (auto *error = maybe_rate.if_error()) {
      std::string prefix;
      prefix +=
          "Unable to parse sample_rate in span sampling rule with span "
          "pattern ";
      prefix += rule.to_json().dump();
      prefix += ": ";
      return error->with_prefix(prefix);
    }

    const auto allowed_types = {FP_NORMAL, FP_SUBNORMAL};
    if (rule.max_per_second &&
        (!(*rule.max_per_second > 0) ||
         std::find(std::begin(allowed_types), std::end(allowed_types),
                   std::fpclassify(*rule.max_per_second)) ==
             std::end(allowed_types))) {
      std::string message;
      message += "Span sampling rule with pattern ";
      message += rule.to_json().dump();
      message +=
          " should have a max_per_second value greater than zero, but the "
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

nlohmann::json to_json(const FinalizedSpanSamplerConfig::Rule &rule) {
  // Get the base class's fields, then add our own.
  auto result = static_cast<const SpanMatcher &>(rule).to_json();
  result["sample_rate"] = double(rule.sample_rate);
  if (rule.max_per_second) {
    result["max_per_second"] = *rule.max_per_second;
  }

  return result;
}

}  // namespace tracing
}  // namespace datadog
