#include "tracer_config.h"

#include <algorithm>
#include <cassert>
#include <cctype>
#include <cstddef>
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

Expected<TracerConfig> load_tracer_env_config(Logger &logger) {
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
    env_cfg.generate_128bit_trace_ids = !falsy(*enabled_env);
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

Expected<FinalizedTracerConfig> finalize_config(const TracerConfig &config) {
  return finalize_config(config, default_clock);
}

Expected<FinalizedTracerConfig> finalize_config(const TracerConfig &user_config,
                                                const Clock &clock) {
  auto logger =
      user_config.logger ? user_config.logger : std::make_shared<CerrLogger>();

  auto env = load_tracer_env_config(*logger);
  if (auto error = env.if_error()) {
    return *error;
  }

  FinalizedTracerConfig final_config;

  final_config.clock = clock;
  final_config.logger = logger;
  final_config.defaults.service =
      value_or(env->service, user_config.service, "");
  final_config.defaults.service_type =
      value_or(env->service_type, user_config.service_type, "web");
  final_config.defaults.environment =
      value_or(env->environment, user_config.environment, "");
  final_config.defaults.version =
      value_or(env->version, user_config.version, "");
  final_config.defaults.name = value_or(env->name, user_config.name, "");
  final_config.defaults.tags =
      value_or(env->tags, user_config.tags,
               std::unordered_map<std::string, std::string>{});
  final_config.extraction_styles =
      value_or(env->extraction_styles, user_config.extraction_styles,
               std::vector<PropagationStyle>{PropagationStyle::DATADOG,
                                             PropagationStyle::W3C});
  final_config.injection_styles =
      value_or(env->injection_styles, user_config.injection_styles,
               std::vector<PropagationStyle>{PropagationStyle::DATADOG,
                                             PropagationStyle::W3C});
  final_config.log_on_startup =
      value_or(env->log_on_startup, user_config.log_on_startup, true);
  final_config.report_traces =
      value_or(env->report_traces, user_config.report_traces, true);
  final_config.report_telemetry =
      value_or(env->report_telemetry, user_config.report_telemetry, true);
  final_config.report_hostname =
      value_or(env->report_hostname, user_config.report_hostname, false);
  final_config.delegate_trace_sampling = value_or(
      env->delegate_trace_sampling, user_config.delegate_trace_sampling, false);
  final_config.tags_header_size = value_or(
      env->max_tags_header_size, user_config.max_tags_header_size, 512);
  final_config.generate_128bit_trace_ids =
      value_or(env->generate_128bit_trace_ids,
               user_config.generate_128bit_trace_ids, true);
  final_config.integration_name =
      value_or(env->integration_name, user_config.integration_name, "");
  final_config.integration_version =
      value_or(env->integration_version, user_config.integration_version, "");

  if (user_config.runtime_id) {
    final_config.runtime_id = user_config.runtime_id;
  }

  if (final_config.defaults.service.empty()) {
    return Error{Error::SERVICE_NAME_REQUIRED, "Service name is required."};
  }

  if (final_config.extraction_styles.empty()) {
    return Error{Error::MISSING_SPAN_EXTRACTION_STYLE,
                 "At least one extraction style must be specified."};
  }
  if (final_config.injection_styles.empty()) {
    return Error{Error::MISSING_SPAN_INJECTION_STYLE,
                 "At least one injection style must be specified."};
  }

  if (!user_config.collector) {
    auto finalized =
        finalize_config(user_config.agent, final_config.logger, clock);
    if (auto *error = finalized.if_error()) {
      return std::move(*error);
    }
    final_config.collector = *finalized;
  } else {
    final_config.collector = user_config.collector;
  }

  if (auto trace_sampler_config = finalize_config(user_config.trace_sampler)) {
    final_config.trace_sampler = std::move(*trace_sampler_config);
  } else {
    return std::move(trace_sampler_config.error());
  }

  if (auto span_sampler_config =
          finalize_config(user_config.span_sampler, *logger)) {
    final_config.span_sampler = std::move(*span_sampler_config);
  } else {
    return std::move(span_sampler_config.error());
  }

  return final_config;
}

}  // namespace tracing
}  // namespace datadog
