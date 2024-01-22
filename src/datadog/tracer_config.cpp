#include "tracer_config.h"

#include <algorithm>
#include <cassert>
#include <cctype>
#include <cstddef>
#include <string>
#include <unordered_map>
#include <variant>
#include <vector>

#include "cerr_logger.h"
#include "collector.h"
#include "datadog/propagation_style.h"
#include "datadog_agent.h"
#include "environment.h"
#include "json.hpp"
#include "null_collector.h"
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

  const char* const end = input.end();

  const char* current = input.begin();
  const char* begin_delim;
  do {
    const char* begin_item =
        std::find_if(current, end, [](uchar ch) { return !std::isspace(ch); });
    begin_delim = std::find_if(begin_item, end, [](uchar ch) {
      return std::isspace(ch) || ch == ',';
    });

    items.emplace_back(begin_item, std::size_t(begin_delim - begin_item));

    const char* end_delim = std::find_if(
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
  for (const StringView& item : parse_list(input)) {
    if (const auto style = parse_propagation_style(item)) {
      styles.push_back(*style);
    } else {
      std::string message;
      message += "Unsupported propagation style \"";
      append(message, item);
      message += "\" in list \"";
      append(message, input);
      message +=
          "\".  The following styles are supported: Datadog, B3,"
          "tracecontext.";
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
  for (const StringView& token : parse_list(input)) {
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
  if (auto* error = styles.if_error()) {
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

Expected<void> finalize_propagation_styles(
    FinalizedTracerConfig& result,
    const std::vector<PropagationStyle>& extraction_styles,
    const std::vector<PropagationStyle>& injection_styles, Logger& logger) {
  namespace env = environment;
  // Print a warning if a questionable combination of environment variables
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
  const std::pair<env::Variable, env::Variable> questionable_combinations[] =
  {
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

  for (const auto& [var, var_override] : questionable_combinations) {
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
                 global_styles, extraction_styles);
    result.injection_styles =
        value_or(styles_from_env(env::DD_TRACE_PROPAGATION_STYLE_INJECT),
                 styles_from_env(env::DD_PROPAGATION_STYLE_INJECT),
                 global_styles, injection_styles);
  } catch (Error& error) {
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

TracerConfig::TracerConfig() {
  // Default values
  config.report_traces = true;
  config.report_telemetry = true;
  config.report_hostname = false;
  config.tags_header_size = 512;
  config.log_on_startup = true;
  config.trace_id_128_bit = true;
  config.injection_styles = {PropagationStyle::DATADOG, PropagationStyle::W3C};
  config.extraction_styles = {PropagationStyle::DATADOG, PropagationStyle::W3C};
  config.delegate_trace_sampling = false;
}

TracerConfig& TracerConfig::service_name(std::string service_name) {
  config.defaults.service = std::move(service_name);
  return *this;
}

TracerConfig& TracerConfig::service_type(std::string service_type) {
  config.defaults.service_type = std::move(service_type);
  return *this;
}

TracerConfig& TracerConfig::service_environment(std::string environment) {
  config.defaults.environment = std::move(environment);
  return *this;
}

TracerConfig& TracerConfig::service_version(std::string version) {
  config.defaults.version = std::move(version);
  return *this;
}

TracerConfig& TracerConfig::span_default_name(std::string default_span_name) {
  config.defaults.name = std::move(default_span_name);
  return *this;
}

TracerConfig& TracerConfig::tags(
    std::unordered_map<std::string, std::string> tags) {
  config.defaults.tags = std::move(tags);
  return *this;
}

TracerConfig& TracerConfig::integration_name(std::string name) {
  config.integration_name = std::move(name);
  return *this;
}

TracerConfig& TracerConfig::integration_version(std::string version) {
  config.integration_version = std::move(version);
  return *this;
}

TracerConfig& TracerConfig::datadog_agent_url(std::string url) {
  agent_config.url = std::move(url);
  return *this;
}

TracerConfig& TracerConfig::report_traces(bool report_traces) {
  config.report_traces = report_traces;
  return *this;
}

TracerConfig& TracerConfig::report_telemetry(bool report_telemetry) {
  config.report_telemetry = report_telemetry;
  return *this;
}

TracerConfig& TracerConfig::enable_128bit_trace_id(
    bool report_128bit_trace_id) {
  config.trace_id_128_bit = report_128bit_trace_id;
  return *this;
}

TracerConfig& TracerConfig::log_configuration_on_startup(bool log_on_startup) {
  config.log_on_startup = log_on_startup;
  return *this;
}

TracerConfig& TracerConfig::injection_styles(
    std::vector<PropagationStyle> injection_styles) {
  config.injection_styles = std::move(injection_styles);
  return *this;
}

TracerConfig& TracerConfig::extraction_styles(
    std::vector<PropagationStyle> extraction_styles) {
  config.extraction_styles = std::move(extraction_styles);
  return *this;
}

TracerConfig& TracerConfig::report_hostname(bool report_hostname) {
  config.report_hostname = report_hostname;
  return *this;
}

TracerConfig& TracerConfig::http_client(
    std::shared_ptr<HTTPClient> http_client) {
  agent_config.http_client = std::move(http_client);
  return *this;
}

TracerConfig& TracerConfig::event_scheduler(
    std::shared_ptr<EventScheduler> event_scheduler) {
  agent_config.event_scheduler = std::move(event_scheduler);
  return *this;
}

TracerConfig& TracerConfig::flush_interval(
    std::chrono::milliseconds flush_interval) {
  agent_config.flush_interval_milliseconds = flush_interval.count();
  return *this;
}

TracerConfig& TracerConfig::request_timeout(
    std::chrono::milliseconds request_timeout) {
  agent_config.request_timeout_milliseconds = request_timeout.count();
  return *this;
}

TracerConfig& TracerConfig::shutdown_timeout(
    std::chrono::milliseconds shutdown_timeout) {
  agent_config.shutdown_timeout_milliseconds = shutdown_timeout.count();
  return *this;
}

TracerConfig& TracerConfig::collector(std::shared_ptr<Collector> collector) {
  config.collector = std::move(collector);
  return *this;
}

TracerConfig& TracerConfig::trace_sampler(TraceSamplerConfig trace_sampler) {
  trace_sampler_ = std::move(trace_sampler);
  return *this;
}

TracerConfig& TracerConfig::span_sampler(SpanSamplerConfig trace_sampler) {
  span_sampler_ = std::move(trace_sampler);
  return *this;
}

TracerConfig& TracerConfig::delegate_trace_sampling(
    bool enable_sampling_deleation) {
  config.delegate_trace_sampling = enable_sampling_deleation;
  return *this;
}

TracerConfig& TracerConfig::max_tags_header_size(
    std::size_t max_tags_header_size) {
  config.tags_header_size = max_tags_header_size;
  return *this;
}

TracerConfig& TracerConfig::logger(std::shared_ptr<Logger> logger) {
  config.logger = std::move(logger);
  return *this;
}

TracerConfig& TracerConfig::runtime_id(RuntimeID runtime_id) {
  config.runtime_id = std::move(runtime_id);
  return *this;
}

TracerConfig& TracerConfig::remote_configuration_poll_interval(
    std::chrono::seconds poll_interval) {
  agent_config.remote_configuration_poll_interval_seconds =
      poll_interval.count();
  return *this;
}

Expected<FinalizedTracerConfig> TracerConfig::finalize() const {
  return finalize(default_clock);
}

Expected<FinalizedTracerConfig> TracerConfig::finalize(
    const Clock& clock) const {
  FinalizedTracerConfig result(config);
  result.clock = clock;

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
    if (auto* error = tags.if_error()) {
      std::string prefix;
      prefix += "Unable to parse ";
      append(prefix, name(environment::DD_TAGS));
      prefix += " environment variable: ";
      return error->with_prefix(prefix);
    }

    result.defaults.tags = std::move(*tags);
  }

  if (auto startup_env = lookup(environment::DD_TRACE_STARTUP_LOGS)) {
    result.log_on_startup = !falsy(*startup_env);
  }

  if (auto enabled_env = lookup(environment::DD_TRACE_ENABLED)) {
    result.report_traces = !falsy(*enabled_env);
  }

  auto finalized = finalize_config(agent_config, result.logger, clock);
  if (auto* error = finalized.if_error()) {
    return std::move(*error);
  }
  result.agent_config = std::move(*finalized);

  if (auto enabled_env =
          lookup(environment::DD_INSTRUMENTATION_TELEMETRY_ENABLED)) {
    result.report_telemetry = !falsy(*enabled_env);
  }

  if (auto trace_delegate_sampling_env =
          lookup(environment::DD_TRACE_DELEGATE_SAMPLING)) {
    result.delegate_trace_sampling = !falsy(*trace_delegate_sampling_env);
  }

  if (auto trace_sampler_config = finalize_config(trace_sampler_)) {
    result.trace_sampler = std::move(*trace_sampler_config);
  } else {
    return std::move(trace_sampler_config.error());
  }

  if (auto span_sampler_config =
          finalize_config(span_sampler_, *result.logger)) {
    result.span_sampler = std::move(*span_sampler_config);
  } else {
    return std::move(span_sampler_config.error());
  }

  auto maybe_error =
      finalize_propagation_styles(result, config.extraction_styles,
                                  config.injection_styles, *result.logger);
  if (!maybe_error) {
    return maybe_error.error();
  }

  if (auto enabled_env =
          lookup(environment::DD_TRACE_128_BIT_TRACEID_GENERATION_ENABLED)) {
    result.trace_id_128_bit = !falsy(*enabled_env);
  }

  return result;
}

}  // namespace tracing
}  // namespace datadog
