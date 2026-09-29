#include <datadog/environment.h>
#include <datadog/expected.h>
#include <datadog/span_sampler_config.h>
#include <datadog/stable_config.h>

#include <cmath>
#include <fstream>
#include <sstream>
#include <unordered_set>

#include "json.hpp"
#include "json_serializer.h"

namespace datadog {
namespace tracing {
namespace {

std::string to_string(const std::vector<SpanSamplerConfig::Rule> &rules) {
  nlohmann::json res;
  for (const auto &r : rules) {
    nlohmann::json j = r;
    j["sample_rate"] = r.sample_rate;
    if (r.max_per_second) {
      j["max_per_second"] = *r.max_per_second;
    }
    res.emplace_back(std::move(j));
  }

  return res.dump();
}

// `env_var` is the name of the environment variable from which `rules_raw` was
// obtained. It's used for error messages.
Expected<std::vector<SpanSamplerConfig::Rule>> parse_rules(StringView rules_raw,
                                                           StringView env_var) {
  std::vector<SpanSamplerConfig::Rule> rules;
  nlohmann::json json_rules;

  try {
    json_rules = nlohmann::json::parse(rules_raw);
  } catch (const nlohmann::json::parse_error &error) {
    std::string message;
    message += "Unable to parse JSON from ";
    append(message, env_var);
    message += " value ";
    append(message, rules_raw);
    message += ": ";
    message += error.what();
    return Error{Error::SPAN_SAMPLING_RULES_INVALID_JSON, std::move(message)};
  }

  std::string type = json_rules.type_name();
  if (type != "array") {
    std::string message;
    message += "Trace sampling rules must be an array, but JSON in ";
    append(message, env_var);
    message += " has type \"";
    message += type;
    message += "\": ";
    append(message, rules_raw);
    return Error{Error::SPAN_SAMPLING_RULES_WRONG_TYPE, std::move(message)};
  }

  const std::unordered_set<std::string> allowed_properties{
      "service", "name", "resource", "tags", "sample_rate", "max_per_second"};

  for (const auto &json_rule : json_rules) {
    auto matcher = from_json(json_rule);
    if (auto *error = matcher.if_error()) {
      std::string prefix;
      prefix += "Unable to create a rule from ";
      append(prefix, env_var);
      prefix += " JSON ";
      append(prefix, rules_raw);
      prefix += ": ";
      return error->with_prefix(prefix);
    }

    SpanSamplerConfig::Rule rule{*matcher};

    auto sample_rate = json_rule.find("sample_rate");
    if (sample_rate != json_rule.end()) {
      type = sample_rate->type_name();
      if (type != "number") {
        std::string message;
        message += "Unable to parse a rule from ";
        append(message, env_var);
        message += " JSON ";
        append(message, rules_raw);
        message += ". The \"sample_rate\" property of the rule ";
        message += json_rule.dump();
        message += " is not a number, but instead has type \"";
        message += type;
        message += "\".";
        return Error{Error::SPAN_SAMPLING_RULES_SAMPLE_RATE_WRONG_TYPE,
                     std::move(message)};
      }
      rule.sample_rate = *sample_rate;
    }

    auto max_per_second = json_rule.find("max_per_second");
    if (max_per_second != json_rule.end()) {
      type = max_per_second->type_name();
      if (type != "number") {
        std::string message;
        message += "Unable to parse a rule from ";
        append(message, env_var);
        message += " JSON ";
        append(message, rules_raw);
        message += ". The \"max_per_second\" property of the rule ";
        message += json_rule.dump();
        message += " is not a number, but instead has type \"";
        message += type;
        message += "\".";
        return Error{Error::SPAN_SAMPLING_RULES_MAX_PER_SECOND_WRONG_TYPE,
                     std::move(message)};
      }
      rule.max_per_second = *max_per_second;
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
      message += ". Error occurred while parsing from ";
      append(message, env_var);
      message += ": ";
      append(message, rules_raw);
      return Error{Error::SPAN_SAMPLING_RULES_UNKNOWN_PROPERTY,
                   std::move(message)};
    }

    rules.emplace_back(std::move(rule));
  }

  return rules;
}

environment::Variable span_rules_source(const StableConfig *stable_config) {
  const auto rules = environment::DD_SPAN_SAMPLING_RULES;
  const auto file = environment::DD_SPAN_SAMPLING_RULES_FILE;
  return config_value_priority(file, stable_config) >
                 config_value_priority(rules, stable_config)
             ? file
             : rules;
}

void log_rule_conflict(Logger &logger, environment::Variable selected) {
  const auto ignored = selected == environment::DD_SPAN_SAMPLING_RULES
                           ? environment::DD_SPAN_SAMPLING_RULES_FILE
                           : environment::DD_SPAN_SAMPLING_RULES;
  std::string message;
  append(message, name(ignored));
  message += " is overridden by ";
  append(message, name(selected));
  message += ". Since both are set, ";
  append(message, name(selected));
  message += " takes precedence, and ";
  append(message, name(ignored));
  message += " will be ignored.";
  logger.log_error(message);
}

Expected<std::vector<SpanSamplerConfig::Rule>> read_span_rules_file(
    StringView path) {
  const auto file_name = std::string(path);
  const auto file_error = [&](const char *operation) {
    std::string message = "Unable to ";
    message += operation;
    message += " file \"" + file_name +
               "\" specified as value of environment variable ";
    append(message, name(environment::DD_SPAN_SAMPLING_RULES_FILE));
    return Error{Error::SPAN_SAMPLING_RULES_FILE_IO, std::move(message)};
  };

  std::ifstream file(file_name);
  if (!file) return file_error("open");
  std::ostringstream stream;
  stream << file.rdbuf();
  if (!file) return file_error("read");

  auto parsed =
      parse_rules(stream.str(), name(environment::DD_SPAN_SAMPLING_RULES_FILE));
  if (auto *error = parsed.if_error()) {
    std::string prefix = "With ";
    append(prefix, name(environment::DD_SPAN_SAMPLING_RULES_FILE));
    prefix += '=';
    append(prefix, path);
    prefix += ": ";
    return error->with_prefix(prefix);
  }
  return *parsed;
}

Expected<SpanSamplerConfig> load_span_sampler_env_config(
    Logger &logger, const StableConfig *stable_config,
    const SpanSamplerConfig &user_config) {
  SpanSamplerConfig env_config;
  if (stable_config &&
      (user_config.rules_configured || !user_config.rules.empty())) {
    return env_config;
  }

  auto rules_env = lookup(environment::DD_SPAN_SAMPLING_RULES, stable_config);
  auto file_env =
      lookup(environment::DD_SPAN_SAMPLING_RULES_FILE, stable_config);
  if (!rules_env && !file_env) return env_config;
  const auto selected = span_rules_source(stable_config);
  if (rules_env && file_env) log_rule_conflict(logger, selected);
  auto parsed = selected == environment::DD_SPAN_SAMPLING_RULES_FILE
                    ? read_span_rules_file(*file_env)
                    : parse_rules(*rules_env, name(selected));
  if (auto *error = parsed.if_error()) return *error;
  env_config.rules = std::move(*parsed);
  env_config.rules_configured = true;

  return env_config;
}

}  // namespace

SpanSamplerConfig::Rule::Rule(const SpanMatcher &base) : SpanMatcher(base) {}

Expected<FinalizedSpanSamplerConfig> finalize_config(
    const SpanSamplerConfig &user_config, Logger &logger) {
  return finalize_config(user_config, logger, nullptr);
}

Expected<FinalizedSpanSamplerConfig> finalize_config(
    const SpanSamplerConfig &user_config, Logger &logger,
    const StableConfig *stable_config) {
  Expected<SpanSamplerConfig> env_config =
      load_span_sampler_env_config(logger, stable_config, user_config);
  if (auto error = env_config.if_error()) {
    return *error;
  }

  FinalizedSpanSamplerConfig result;
  Optional<std::vector<SpanSamplerConfig::Rule>> env_rules;
  Optional<std::vector<SpanSamplerConfig::Rule>> user_rules;
  if (env_config->rules_configured || !env_config->rules.empty()) {
    env_rules = env_config->rules;
  }
  if (user_config.rules_configured || !user_config.rules.empty()) {
    user_rules = user_config.rules;
  }

  std::vector<SpanSamplerConfig::Rule> rules = resolve_with_stable_config(
      env_rules, user_rules, &result.metadata, ConfigName::SPAN_SAMPLING_RULES,
      span_rules_source(stable_config), stable_config, nullptr,
      [](const std::vector<SpanSamplerConfig::Rule> &r) {
        return to_string(r);
      });

  for (const auto &rule : rules) {
    auto maybe_rate = Rate::from(rule.sample_rate);
    if (auto *error = maybe_rate.if_error()) {
      std::string prefix;
      prefix +=
          "Unable to parse sample_rate in span sampling rule with span "
          "pattern ";
      prefix += nlohmann::json(static_cast<SpanMatcher>(rule)).dump();
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
      message += nlohmann::json(static_cast<SpanMatcher>(rule)).dump();
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

std::string to_string(const FinalizedSpanSamplerConfig::Rule &rule) {
  // Get the base class's fields, then add our own.
  nlohmann::json result = static_cast<const SpanMatcher &>(rule);
  result["sample_rate"] = double(rule.sample_rate);
  if (rule.max_per_second) {
    result["max_per_second"] = *rule.max_per_second;
  }

  return result.dump();
}

}  // namespace tracing
}  // namespace datadog
