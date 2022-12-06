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

  if (auto rules_env = lookup(environment::DD_TRACE_SAMPLING_RULES)) {
    rules.clear();
    nlohmann::json json_rules;
    try {
      json_rules = nlohmann::json::parse(*rules_env);
    } catch (const nlohmann::json::parse_error &error) {
      std::string message;
      message += "Unable to parse JSON from ";
      append(message, name(environment::DD_TRACE_SAMPLING_RULES));
      message += " value ";
      append(message, *rules_env);
      message += ": ";
      message += error.what();
      return Error{Error::TRACE_SAMPLING_RULES_INVALID_JSON,
                   std::move(message)};
    }

    std::string type = json_rules.type_name();
    if (type != "array") {
      std::string message;
      message += "Trace sampling rules must be an array, but ";
      append(message, name(environment::DD_TRACE_SAMPLING_RULES));
      message += " has JSON type \"";
      message += type;
      message += "\": ";
      append(message, *rules_env);
      return Error{Error::TRACE_SAMPLING_RULES_WRONG_TYPE, std::move(message)};
    }

    const std::unordered_set<std::string> allowed_properties{
        "service", "name", "resource", "tags", "sample_rate"};

    for (const auto &json_rule : json_rules) {
      auto matcher = SpanMatcher::from_json(json_rule);
      if (auto *error = matcher.if_error()) {
        std::string prefix;
        prefix += "Unable to create a rule from ";
        append(prefix, name(environment::DD_TRACE_SAMPLING_RULES));
        prefix += " value ";
        append(prefix, *rules_env);
        prefix += ": ";
        return error->with_prefix(prefix);
      }

      TraceSamplerConfig::Rule rule{*matcher};

      auto sample_rate = json_rule.find("sample_rate");
      if (sample_rate != json_rule.end()) {
        type = sample_rate->type_name();
        if (type != "number") {
          std::string message;
          message += "Unable to parse a rule from ";
          append(message, name(environment::DD_TRACE_SAMPLING_RULES));
          message += " value ";
          append(message, *rules_env);
          message += ".  The \"sample_rate\" property of the rule ";
          message += json_rule.dump();
          message += " is not a number, but instead has type \"";
          message += type;
          message += "\".";
          return Error{Error::TRACE_SAMPLING_RULES_SAMPLE_RATE_WRONG_TYPE,
                       std::move(message)};
        }
        rule.sample_rate = *sample_rate;
      }

      // Look for unexpected properties.
      for (const auto &[key, value] : json_rule.items()) {
        if (allowed_properties.count(key)) {
          continue;
        }
        std::string message;
        message += "Unexpected property \"";
        message += key;
        message += "\" having value ";
        message += value.dump();
        message += " in trace sampling rule ";
        message += json_rule.dump();
        message += ".  Error occurred while parsing ";
        append(message, name(environment::DD_TRACE_SAMPLING_RULES));
        message += ": ";
        append(message, *rules_env);
        return Error{Error::TRACE_SAMPLING_RULES_UNKNOWN_PROPERTY,
                     std::move(message)};
      }

      rules.emplace_back(std::move(rule));
    }
  }

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
  if (auto sample_rate_env = lookup(environment::DD_TRACE_SAMPLE_RATE)) {
    auto maybe_sample_rate = parse_double(*sample_rate_env);
    if (auto *error = maybe_sample_rate.if_error()) {
      std::string prefix;
      prefix += "While parsing ";
      append(prefix, name(environment::DD_TRACE_SAMPLE_RATE));
      prefix += ": ";
      return error->with_prefix(prefix);
    }
    sample_rate = *maybe_sample_rate;
  }

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

  auto max_per_second = config.max_per_second;
  if (auto limit_env = lookup(environment::DD_TRACE_RATE_LIMIT)) {
    auto maybe_max_per_second = parse_double(*limit_env);
    if (auto *error = maybe_max_per_second.if_error()) {
      std::string prefix;
      prefix += "While parsing ";
      append(prefix, name(environment::DD_TRACE_RATE_LIMIT));
      prefix += ": ";
      return error->with_prefix(prefix);
    }
    max_per_second = *maybe_max_per_second;
  }

  const auto allowed_types = {FP_NORMAL, FP_SUBNORMAL};
  if (!(max_per_second > 0) ||
      std::find(std::begin(allowed_types), std::end(allowed_types),
                std::fpclassify(max_per_second)) == std::end(allowed_types)) {
    std::string message;
    message +=
        "Trace sampling max_per_second must be greater than zero, but the "
        "following value was given: ";
    message += std::to_string(config.max_per_second);
    return Error{Error::MAX_PER_SECOND_OUT_OF_RANGE, std::move(message)};
  }
  result.max_per_second = max_per_second;

  return result;
}

nlohmann::json to_json(const FinalizedTraceSamplerConfig::Rule &rule) {
  return nlohmann::json::object({
      {"service", rule.service},
      {"name", rule.name},
      {"resource", rule.resource},
      {"sample_rate", double(rule.sample_rate)},
  });
}

}  // namespace tracing
}  // namespace datadog
