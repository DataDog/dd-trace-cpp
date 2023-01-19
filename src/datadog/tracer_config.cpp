#include "tracer_config.h"

#include <algorithm>
#include <cassert>
#include <cctype>
#include <cstddef>
#include <string>
#include <unordered_map>
#include <vector>

#include "cerr_logger.h"
#include "datadog_agent.h"
#include "environment.h"
#include "json.hpp"
#include "null_collector.h"
#include "parse_util.h"
#include "string_view.h"

namespace datadog {
namespace tracing {
namespace {

void to_lower(std::string &text) {
  std::transform(text.begin(), text.end(), text.begin(),
                 [](unsigned char ch) { return std::tolower(ch); });
}

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
    auto token = std::string(item);
    to_lower(token);
    // Note: Make sure that these strings are consistent (modulo case) with
    // `to_json(PropagationStyle)` in `propagation_style.cpp`.
    if (token == "datadog") {
      styles.push_back(PropagationStyle::DATADOG);
    } else if (token == "b3" || token == "b3multi") {
      styles.push_back(PropagationStyle::B3);
    } else if (token ==
               "tracecontext") {  // for compatibility with OpenTelemetry
      styles.push_back(PropagationStyle::W3C);
    } else if (token == "none") {
      styles.push_back(PropagationStyle::NONE);
    } else {
      std::string message;
      message += "Unsupported propagation style \"";
      message += token;
      message += "\" in list \"";
      append(message, input);
      message += "\".  The following styles are supported: Datadog, B3.";
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

Expected<void> finalize_propagation_styles(FinalizedTracerConfig &result,
                                           const TracerConfig &config,
                                           Logger &logger) {
  namespace env = environment;
  // Print a warning if a questionable combination of environment variables is
  // defined.
  const auto ts = env::DD_TRACE_PROPAGATION_STYLE;
  const auto tse = env::DD_TRACE_PROPAGATION_STYLE_EXTRACT;
  const auto se = env::DD_PROPAGATION_STYLE_EXTRACT;
  const auto tsi = env::DD_TRACE_PROPAGATION_STYLE_INJECT;
  const auto si = env::DD_PROPAGATION_STYLE_INJECT;
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
  const std::pair<env::Variable, env::Variable> questionable_combinations[] = {
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
    const auto var_name = name(var);
    const auto var_name_override = name(var_override);
    logger.log_error(Error{
        Error::MULTIPLE_PROPAGATION_STYLE_ENVIRONMENT_VARIABLES,
        warn_message(var_name, *value, var_name_override, *value_override)});
  }

  // Parse the propagation styles from the configuration and/or from the
  // environment.
  // Exceptions make this section simpler.
  try {
    const auto global_styles = styles_from_env(env::DD_TRACE_PROPAGATION_STYLE);
    result.extraction_styles =
        value_or(styles_from_env(env::DD_TRACE_PROPAGATION_STYLE_EXTRACT),
                 styles_from_env(env::DD_PROPAGATION_STYLE_EXTRACT),
                 global_styles, config.extraction_styles);
    result.injection_styles =
        value_or(styles_from_env(env::DD_TRACE_PROPAGATION_STYLE_INJECT),
                 styles_from_env(env::DD_PROPAGATION_STYLE_INJECT),
                 global_styles, config.injection_styles);
  } catch (Error &error) {
    return std::move(error);
  }

  if (result.extraction_styles.empty()) {
    return Error{Error::MISSING_SPAN_EXTRACTION_STYLE,
                 "At least one extraction style must be specified."};
  }
  if (result.injection_styles.empty()) {
    return Error{Error::MISSING_SPAN_INJECTION_STYLE,
                 "At least one injection style must be specified."};
  }

  return {};
}

}  // namespace

Expected<FinalizedTracerConfig> finalize_config(const TracerConfig &config) {
  FinalizedTracerConfig result;

  result.defaults = config.defaults;

  if (auto service_env = lookup(environment::DD_SERVICE)) {
    assign(result.defaults.service, *service_env);
  }
  if (result.defaults.service.empty()) {
    return Error{Error::SERVICE_NAME_REQUIRED, "Service name is required."};
  }

  if (auto environment_env = lookup(environment::DD_ENV)) {
    assign(result.defaults.environment, *environment_env);
  }
  if (auto version_env = lookup(environment::DD_VERSION)) {
    assign(result.defaults.version, *version_env);
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
    result.defaults.tags = std::move(*tags);
  }

  if (config.logger) {
    result.logger = config.logger;
  } else {
    result.logger = std::make_shared<CerrLogger>();
  }

  result.log_on_startup = config.log_on_startup;
  if (auto startup_env = lookup(environment::DD_TRACE_STARTUP_LOGS)) {
    result.log_on_startup = !falsy(*startup_env);
  }

  bool report_traces = config.report_traces;
  if (auto enabled_env = lookup(environment::DD_TRACE_ENABLED)) {
    report_traces = !falsy(*enabled_env);
  }

  if (!report_traces) {
    result.collector = std::make_shared<NullCollector>();
  } else if (!config.collector) {
    auto finalized = finalize_config(config.agent, result.logger);
    if (auto *error = finalized.if_error()) {
      return std::move(*error);
    }
    result.collector = *finalized;
  } else {
    result.collector = config.collector;
  }

  if (auto trace_sampler_config = finalize_config(config.trace_sampler)) {
    result.trace_sampler = std::move(*trace_sampler_config);
  } else {
    return std::move(trace_sampler_config.error());
  }

  if (auto span_sampler_config =
          finalize_config(config.span_sampler, *result.logger)) {
    result.span_sampler = std::move(*span_sampler_config);
  } else {
    return std::move(span_sampler_config.error());
  }

  auto maybe_error =
      finalize_propagation_styles(result, config, *result.logger);
  if (!maybe_error) {
    return maybe_error.error();
  }

  result.report_hostname = config.report_hostname;
  result.tags_header_size = config.tags_header_size;

  if (auto enabled_env =
          lookup(environment::DD_TRACE_128_BIT_TRACEID_GENERATION_ENABLED)) {
    result.trace_id_128_bit = !falsy(*enabled_env);
  } else {
    result.trace_id_128_bit = config.trace_id_128_bit;
  }

  return result;
}

}  // namespace tracing
}  // namespace datadog
