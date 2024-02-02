#include "tracer_config.h"

#include <algorithm>
#include <cassert>
#include <cctype>
#include <cstddef>
#include <fstream>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "cerr_logger.h"
#include "datadog_agent.h"
#include "environment.h"
#include "json.hpp"
#include "parse_util.h"
#include "string_view.h"

namespace datadog {
namespace tracing {
namespace {

bool falsy(StringView text) {
  auto lower = std::string{text};
  to_lower(lower);
  return lower == "0" || lower == "false" || lower == "no";
}

// List items are separated by an optional comma (",") and any amount of
// whitespace.
// Leading and trailing whitespace is ignored.
std::vector<StringView> parse_list(StringView input) {
  using uchar = unsigned char;

  input = strip(input);
  std::vector<StringView> items;
  if (input.empty()) {
    return items;
  }

  const char *const end = input.end();

  const char *current = input.begin();
  const char *begin_delim;
  do {
    const char *begin_item =
        std::find_if(current, end, [](uchar ch) { return !std::isspace(ch); });
    begin_delim = std::find_if(begin_item, end, [](uchar ch) {
      return std::isspace(ch) || ch == ',';
    });

    items.emplace_back(begin_item, std::size_t(begin_delim - begin_item));

    const char *end_delim = std::find_if(
        begin_delim, end, [](uchar ch) { return !std::isspace(ch); });

    if (end_delim != end && *end_delim == ',') {
      ++end_delim;
    }

    current = end_delim;
  } while (begin_delim != end);

  return items;
}

Expected<std::vector<PropagationStyle>> parse_propagation_styles(
    StringView input) {
  std::vector<PropagationStyle> styles;

  const auto last_is_duplicate = [&]() -> Optional<Error> {
    assert(!styles.empty());

    const auto dupe =
        std::find(styles.begin(), styles.end() - 1, styles.back());
    if (dupe == styles.end() - 1) {
      return nullopt;  // no duplicate
    }

    std::string message;
    message += "The propagation style ";
    message += to_json(styles.back()).dump();
    message += " is duplicated in: ";
    append(message, input);
    return Error{Error::DUPLICATE_PROPAGATION_STYLE, std::move(message)};
  };

  // Style names are separated by spaces, or a comma, or some combination.
  for (const StringView &item : parse_list(input)) {
    if (const auto style = parse_propagation_style(item)) {
      styles.push_back(*style);
    } else {
      std::string message;
      message += "Unsupported propagation style \"";
      append(message, item);
      message += "\" in list \"";
      append(message, input);
      message +=
          "\".  The following styles are supported: Datadog, B3, tracecontext.";
      return Error{Error::UNKNOWN_PROPAGATION_STYLE, std::move(message)};
    }

    if (auto maybe_error = last_is_duplicate()) {
      return *maybe_error;
    }
  }

  return styles;
}

Expected<std::unordered_map<std::string, std::string>> parse_tags(
    StringView input) {
  std::unordered_map<std::string, std::string> tags;

  // Within a tag, the key and value are separated by a colon (":").
  for (const StringView &token : parse_list(input)) {
    const auto separator = std::find(token.begin(), token.end(), ':');
    if (separator == token.end()) {
      std::string message;
      message += "Unable to parse a key/value from the tag text \"";
      append(message, token);
      message +=
          "\" because it does not contain the separator character \":\".  "
          "Error occurred in list of tags \"";
      append(message, input);
      message += "\".";
      return Error{Error::TAG_MISSING_SEPARATOR, std::move(message)};
    }
    std::string key{token.begin(), separator};
    std::string value{separator + 1, token.end()};
    // If there are duplicate values, then the last one wins.
    tags.insert_or_assign(std::move(key), std::move(value));
  }

  return tags;
}

// Return a `std::vector<PropagationStyle>` parsed from the specified `env_var`.
// If `env_var` is not in the environment, return `nullopt`. If an error occurs,
// throw an `Error`.
Optional<std::vector<PropagationStyle>> styles_from_env(
    environment::Variable env_var) {
  const auto styles_env = lookup(env_var);
  if (!styles_env) {
    return {};
  }

  auto styles = parse_propagation_styles(*styles_env);
  if (auto *error = styles.if_error()) {
    std::string prefix;
    prefix += "Unable to parse ";
    append(prefix, name(env_var));
    prefix += " environment variable: ";
    throw error->with_prefix(prefix);
  }
  return *styles;
}

std::string json_quoted(StringView text) {
  std::string unquoted;
  assign(unquoted, text);
  return nlohmann::json(std::move(unquoted)).dump();
}

// `env_var` is the name of the environment variable from which `rules_raw` was
// obtained.  It's used for error messages.
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
    auto matcher = SpanMatcher::from_json(json_rule);
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
        message += ".  The \"sample_rate\" property of the rule ";
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
        message += ".  The \"max_per_second\" property of the rule ";
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
      message += ".  Error occurred while parsing from ";
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

Expected<TracerConfig> load_env_config(Logger &logger) {
  TracerConfig env_cfg;

  if (auto service_env = lookup(environment::DD_SERVICE)) {
    env_cfg.service = std::string{*service_env};
  }

  if (auto environment_env = lookup(environment::DD_ENV)) {
    env_cfg.environment = std::string{*environment_env};
  }
  if (auto version_env = lookup(environment::DD_VERSION)) {
    env_cfg.version = std::string{*version_env};
  }

  if (auto tags_env = lookup(environment::DD_TAGS)) {
    auto tags = parse_tags(*tags_env);
    if (auto *error = tags.if_error()) {
      std::string prefix;
      prefix += "Unable to parse ";
      append(prefix, name(environment::DD_TAGS));
      prefix += " environment variable: ";
      return error->with_prefix(prefix);
    }
    env_cfg.tags = std::move(*tags);
  }

  if (auto startup_env = lookup(environment::DD_TRACE_STARTUP_LOGS)) {
    env_cfg.log_on_startup = !falsy(*startup_env);
  }
  if (auto enabled_env = lookup(environment::DD_TRACE_ENABLED)) {
    env_cfg.report_traces = !falsy(*enabled_env);
  }
  if (auto enabled_env =
          lookup(environment::DD_INSTRUMENTATION_TELEMETRY_ENABLED)) {
    env_cfg.report_telemetry = !falsy(*enabled_env);
  }
  if (auto trace_delegate_sampling_env =
          lookup(environment::DD_TRACE_DELEGATE_SAMPLING)) {
    env_cfg.delegate_trace_sampling = !falsy(*trace_delegate_sampling_env);
  }
  if (auto enabled_env =
          lookup(environment::DD_TRACE_128_BIT_TRACEID_GENERATION_ENABLED)) {
    env_cfg.trace_id_128_bit = !falsy(*enabled_env);
  }

  if (auto rules_env = lookup(environment::DD_TRACE_SAMPLING_RULES)) {
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

      env_cfg.trace_sampler.rules.emplace_back(std::move(rule));
    }
  }

  if (auto sample_rate_env = lookup(environment::DD_TRACE_SAMPLE_RATE)) {
    auto maybe_sample_rate = parse_double(*sample_rate_env);
    if (auto *error = maybe_sample_rate.if_error()) {
      std::string prefix;
      prefix += "While parsing ";
      append(prefix, name(environment::DD_TRACE_SAMPLE_RATE));
      prefix += ": ";
      return error->with_prefix(prefix);
    }
    env_cfg.trace_sampler.sample_rate = *maybe_sample_rate;
  }

  if (auto limit_env = lookup(environment::DD_TRACE_RATE_LIMIT)) {
    auto maybe_max_per_second = parse_double(*limit_env);
    if (auto *error = maybe_max_per_second.if_error()) {
      std::string prefix;
      prefix += "While parsing ";
      append(prefix, name(environment::DD_TRACE_RATE_LIMIT));
      prefix += ": ";
      return error->with_prefix(prefix);
    }
    env_cfg.trace_sampler.max_per_second = *maybe_max_per_second;
  }

  // SpanSampler
  auto rules_env = lookup(environment::DD_SPAN_SAMPLING_RULES);
  if (rules_env) {
    auto maybe_rules =
        parse_rules(*rules_env, name(environment::DD_SPAN_SAMPLING_RULES));
    if (auto *error = maybe_rules.if_error()) {
      return std::move(*error);
    }
    env_cfg.span_sampler.rules = std::move(*maybe_rules);
  }

  if (auto file_env = lookup(environment::DD_SPAN_SAMPLING_RULES_FILE)) {
    if (rules_env) {
      const auto rules_file_name =
          name(environment::DD_SPAN_SAMPLING_RULES_FILE);
      const auto rules_name = name(environment::DD_SPAN_SAMPLING_RULES);
      std::string message;
      append(message, rules_file_name);
      message += " is overridden by ";
      append(message, rules_name);
      message += ".  Since both are set, ";
      append(message, rules_name);
      message += " takes precedence, and ";
      append(message, rules_file_name);
      message += " will be ignored.";
      logger.log_error(message);
    } else {
      const auto span_rules_file = std::string(*file_env);

      const auto file_error = [&](const char *operation) {
        std::string message;
        message += "Unable to ";
        message += operation;
        message += " file \"";
        message += span_rules_file;
        message += "\" specified as value of environment variable ";
        append(message, name(environment::DD_SPAN_SAMPLING_RULES_FILE));

        return Error{Error::SPAN_SAMPLING_RULES_FILE_IO, std::move(message)};
      };

      std::ifstream file(span_rules_file);
      if (!file) {
        return file_error("open");
      }

      std::ostringstream rules_stream;
      rules_stream << file.rdbuf();
      if (!file) {
        return file_error("read");
      }

      auto maybe_rules = parse_rules(
          rules_stream.str(), name(environment::DD_SPAN_SAMPLING_RULES_FILE));
      if (auto *error = maybe_rules.if_error()) {
        std::string prefix;
        prefix += "With ";
        append(prefix, name(environment::DD_SPAN_SAMPLING_RULES_FILE));
        prefix += '=';
        append(prefix, *file_env);
        prefix += ": ";
        return error->with_prefix(prefix);
      }

      env_cfg.span_sampler.rules = std::move(*maybe_rules);
    }
  }

  // DatadogAgentConfig
  if (auto raw_rc_poll_interval_value =
          lookup(environment::DD_REMOTE_CONFIG_POLL_INTERVAL_SECONDS)) {
    auto res = parse_int(*raw_rc_poll_interval_value, 10);
    if (auto error = res.if_error()) {
      return error->with_prefix(
          "DatadogAgent: Remote Configuration poll interval error ");
    }

    env_cfg.agent.remote_configuration_poll_interval_seconds = *res;
  }

  auto env_host = lookup(environment::DD_AGENT_HOST);
  auto env_port = lookup(environment::DD_TRACE_AGENT_PORT);

  if (auto url_env = lookup(environment::DD_TRACE_AGENT_URL)) {
    env_cfg.agent.url = std::string{*url_env};
  } else if (env_host || env_port) {
    std::string configured_url = "http://";
    append(configured_url, env_host.value_or("localhost"));
    configured_url += ':';
    append(configured_url, env_port.value_or("8126"));

    env_cfg.agent.url = std::move(configured_url);
  }

  // PropagationStyle
  // Print a warning if a questionable combination of environment variables is
  // defined.
  const auto ts = environment::DD_TRACE_PROPAGATION_STYLE;
  const auto tse = environment::DD_TRACE_PROPAGATION_STYLE_EXTRACT;
  const auto se = environment::DD_PROPAGATION_STYLE_EXTRACT;
  const auto tsi = environment::DD_TRACE_PROPAGATION_STYLE_INJECT;
  const auto si = environment::DD_PROPAGATION_STYLE_INJECT;
  // clang-format off
  /*
           ts    tse   se    tsi   si
           ---   ---   ---   ---   ---
    ts  |  x     warn  warn  warn  warn
        |
    tse |  x     x     warn  ok    ok
        |
    se  |  x     x     x     ok    ok
        |
    tsi |  x     x     x     x     warn
        |
    si  |  x     x     x     x     x
  */
  // In each pair, the first would be overridden by the second.
  const std::pair<environment::Variable, environment::Variable> questionable_combinations[] = {
           {ts, tse}, {ts, se},  {ts, tsi}, {ts, si},

                      {se, tse}, /* ok */   /* ok */

                                 /* ok */   /* ok */

                                            {si, tsi},
  };
  // clang-format on

  const auto warn_message = [](StringView name, StringView value,
                               StringView name_override,
                               StringView value_override) {
    std::string message;
    message += "Both the environment variables ";
    append(message, name);
    message += "=";
    message += json_quoted(value);
    message += " and ";
    append(message, name_override);
    message += "=";
    message += json_quoted(value_override);
    message += " are defined. ";
    append(message, name_override);
    message += " will take precedence.";
    return message;
  };

  for (const auto &[var, var_override] : questionable_combinations) {
    const auto value = lookup(var);
    if (!value) {
      continue;
    }
    const auto value_override = lookup(var_override);
    if (!value_override) {
      continue;
    }
    // TODO: log
    const auto var_name = name(var);
    const auto var_name_override = name(var_override);

    logger.log_error(Error{
        Error::MULTIPLE_PROPAGATION_STYLE_ENVIRONMENT_VARIABLES,
        warn_message(var_name, *value, var_name_override, *value_override)});
  }

  try {
    const auto global_styles =
        styles_from_env(environment::DD_TRACE_PROPAGATION_STYLE);

    if (auto propagation_value =
            styles_from_env(environment::DD_TRACE_PROPAGATION_STYLE_EXTRACT)) {
      env_cfg.extraction_styles = std::move(*propagation_value);
    } else if (auto propagation_value =
                   styles_from_env(environment::DD_PROPAGATION_STYLE_EXTRACT)) {
      env_cfg.extraction_styles = std::move(*propagation_value);
    } else {
      env_cfg.extraction_styles = global_styles;
    }

    if (auto injection_styles =
            styles_from_env(environment::DD_TRACE_PROPAGATION_STYLE_INJECT)) {
      env_cfg.injection_styles = std::move(*injection_styles);
    } else if (auto injection_styles =
                   styles_from_env(environment::DD_PROPAGATION_STYLE_INJECT)) {
      env_cfg.injection_styles = std::move(*injection_styles);
    } else {
      env_cfg.injection_styles = global_styles;
    }
  } catch (Error &error) {
    return std::move(error);
  }

  return env_cfg;
}

}  // namespace

static const TracerDefaultConfig k_default_cfg;

Expected<FinalizedTracerConfig> finalize_config(const TracerConfig &config) {
  return finalize_config(config, k_default_cfg, default_clock);
}

Expected<FinalizedTracerConfig> finalize_config(const TracerConfig &user_config,
                                                const Clock &clock) {
  return finalize_config(user_config, k_default_cfg, clock);
}

Expected<FinalizedTracerConfig> finalize_config(
    const TracerConfig &config, const TracerDefaultConfig &default_config) {
  return finalize_config(config, default_config, default_clock);
}

Expected<FinalizedTracerConfig> finalize_config(
    const TracerConfig &user_config, const TracerDefaultConfig &default_config,
    const Clock &clock) {
  FinalizedTracerConfig final_cfg;

  if (user_config.logger) {
    final_cfg.logger = user_config.logger;
  } else {
    final_cfg.logger = std::make_shared<CerrLogger>();
  }

  auto env = load_env_config(*final_cfg.logger);
  if (auto error = env.if_error()) {
    return *error;
  }

  final_cfg.clock = clock;
  final_cfg.defaults.service = value_or(env->service, user_config.service, "");
  final_cfg.defaults.service_type = value_or(
      env->service_type, user_config.service_type, default_config.service_type);
  final_cfg.defaults.environment =
      value_or(env->environment, user_config.environment, "");
  final_cfg.defaults.version = value_or(env->version, user_config.version, "");
  final_cfg.defaults.name = value_or(env->name, user_config.name, "");

  std::unordered_map<std::string, std::string> default_tags;
  final_cfg.defaults.tags = value_or(env->tags, user_config.tags, default_tags);

  final_cfg.extraction_styles =
      value_or(env->extraction_styles, user_config.extraction_styles,
               default_config.extraction_styles);
  final_cfg.injection_styles =
      value_or(env->injection_styles, user_config.injection_styles,
               default_config.injection_styles);

  final_cfg.log_on_startup =
      value_or(env->log_on_startup, user_config.log_on_startup,
               default_config.log_on_startup);
  final_cfg.report_traces =
      value_or(env->report_traces, user_config.report_traces,
               default_config.report_traces);
  final_cfg.report_telemetry =
      value_or(env->report_telemetry, user_config.report_telemetry,
               default_config.report_telemetry);
  final_cfg.report_hostname =
      value_or(env->report_hostname, user_config.report_hostname,
               default_config.report_hostname);
  final_cfg.delegate_trace_sampling = value_or(
      env->delegate_trace_sampling, user_config.delegate_trace_sampling,
      default_config.delegate_trace_sampling);
  final_cfg.tags_header_size =
      value_or(env->tags_header_size, user_config.tags_header_size,
               default_config.max_tags_header_size);
  final_cfg.trace_id_128_bit =
      value_or(env->trace_id_128_bit, user_config.trace_id_128_bit,
               default_config.generate_128bit_trace_ids);
  final_cfg.integration_name =
      value_or(env->integration_name, user_config.integration_name, "");
  final_cfg.integration_version =
      value_or(env->integration_version, user_config.integration_version, "");

  if (user_config.runtime_id) {
    final_cfg.runtime_id = user_config.runtime_id;
  }

  DatadogAgentConfig merged_agent_config;
  merged_agent_config.url =
      value_or(env->agent.url, user_config.agent.url, default_config.agent_url);
  merged_agent_config.flush_interval_milliseconds =
      value_or(env->agent.flush_interval_milliseconds,
               user_config.agent.flush_interval_milliseconds,
               default_config.flush_interval_milliseconds);
  merged_agent_config.shutdown_timeout_milliseconds =
      value_or(env->agent.shutdown_timeout_milliseconds,
               user_config.agent.shutdown_timeout_milliseconds,
               default_config.shutdown_timeout_milliseconds);
  merged_agent_config.request_timeout_milliseconds =
      value_or(env->agent.request_timeout_milliseconds,
               user_config.agent.request_timeout_milliseconds,
               default_config.request_timeout_milliseconds);
  merged_agent_config.remote_configuration_poll_interval_seconds =
      value_or(env->agent.remote_configuration_poll_interval_seconds,
               user_config.agent.remote_configuration_poll_interval_seconds,
               default_config.remote_configuration_poll_interval_seconds);
  if (user_config.agent.http_client) {
    merged_agent_config.http_client = user_config.agent.http_client;
  }
  if (user_config.agent.event_scheduler) {
    merged_agent_config.event_scheduler = user_config.agent.event_scheduler;
  }

  TraceSamplerConfig ts;
  if (env->trace_sampler.sample_rate) {
    ts.sample_rate = env->trace_sampler.sample_rate;
  } else if (user_config.trace_sampler.sample_rate) {
    ts.sample_rate = user_config.trace_sampler.sample_rate;
  }

  if (!env->trace_sampler.rules.empty()) {
    ts.rules = env->trace_sampler.rules;
  } else {
    ts.rules = user_config.trace_sampler.rules;
  }

  ts.max_per_second = value_or(env->trace_sampler.max_per_second,
                               user_config.trace_sampler.max_per_second, 200);

  SpanSamplerConfig ss;
  if (!env->span_sampler.rules.empty()) {
    ss.rules = env->span_sampler.rules;
  } else {
    ss.rules = user_config.span_sampler.rules;
  }

  if (final_cfg.defaults.service.empty()) {
    return Error{Error::SERVICE_NAME_REQUIRED, "Service name is required."};
  }

  if (final_cfg.extraction_styles.empty()) {
    return Error{Error::MISSING_SPAN_EXTRACTION_STYLE,
                 "At least one extraction style must be specified."};
  }
  if (final_cfg.injection_styles.empty()) {
    return Error{Error::MISSING_SPAN_INJECTION_STYLE,
                 "At least one injection style must be specified."};
  }

  if (!user_config.collector) {
    auto finalized =
        finalize_config(merged_agent_config, final_cfg.logger, clock);
    if (auto *error = finalized.if_error()) {
      return std::move(*error);
    }
    final_cfg.collector = *finalized;
  } else {
    final_cfg.collector = user_config.collector;
  }

  if (auto trace_sampler_config = finalize_config(ts)) {
    final_cfg.trace_sampler = std::move(*trace_sampler_config);
  } else {
    return std::move(trace_sampler_config.error());
  }

  if (auto span_sampler_config = finalize_config(ss)) {
    final_cfg.span_sampler = std::move(*span_sampler_config);
  } else {
    return std::move(span_sampler_config.error());
  }

  return final_cfg;
}

}  // namespace tracing
}  // namespace datadog
