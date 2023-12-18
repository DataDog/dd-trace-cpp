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
#include "datadog/tracer_telemetry.h"
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
  log_configuration_on_startup(true, ConfigOrigin::DEFAULT);
  enable_traces(true, ConfigOrigin::DEFAULT);
  enable_telemetry(true, ConfigOrigin::DEFAULT);
  enable_hostname_in_span(false, ConfigOrigin::DEFAULT);
  enable_128bit_trace_id(true, ConfigOrigin::DEFAULT);
  set_injection_styles({PropagationStyle::DATADOG, PropagationStyle::W3C},
                       ConfigOrigin::DEFAULT);
  set_extraction_styles({PropagationStyle::DATADOG, PropagationStyle::W3C},
                        ConfigOrigin::DEFAULT);
  set_max_tags_header_size(512, ConfigOrigin::DEFAULT);
}

TracerConfig& TracerConfig::set_service_name(std::string service_name,
                                             ConfigOrigin origin) {
  set_telemetry(name(environment::DD_SERVICE), service_name, origin);
  config.defaults.service = std::move(service_name);
  return *this;
}

TracerConfig& TracerConfig::set_service_type(std::string service_type,
                                             ConfigOrigin) {
  config.defaults.service_type = std::move(service_type);
  return *this;
}

TracerConfig& TracerConfig::set_environment(std::string environment,
                                            ConfigOrigin origin) {
  set_telemetry(name(environment::DD_ENV), environment, origin);
  config.defaults.environment = std::move(environment);
  return *this;
}

TracerConfig& TracerConfig::set_version(std::string version,
                                        ConfigOrigin origin) {
  set_telemetry(name(environment::DD_VERSION), version, origin);
  config.defaults.version = std::move(version);
  return *this;
}

TracerConfig& TracerConfig::set_span_default_name(std::string default_span_name,
                                                  ConfigOrigin) {
  config.defaults.name = std::move(default_span_name);
  return *this;
}

TracerConfig& TracerConfig::set_tags(
    std::unordered_map<std::string, std::string> tags, ConfigOrigin) {
  config.defaults.tags = std::move(tags);
  // set_telemetry(name(environment::DD_TAGS),
  //               stringutil::join(config.defaults.tags), ConfigOrigin::CODE);
  return *this;
}

TracerConfig& TracerConfig::set_datadog_agent_url(std::string url,
                                                  ConfigOrigin origin) {
  set_telemetry(name(environment::DD_TRACE_AGENT_URL), url, origin);
  agent_config.url = std::move(url);
  return *this;
}

TracerConfig& TracerConfig::enable_traces(bool report_traces,
                                          ConfigOrigin origin) {
  set_telemetry(name(environment::DD_TRACE_ENABLED),
                std::to_string(report_traces), origin);
  config.report_traces = report_traces;
  return *this;
}

TracerConfig& TracerConfig::enable_telemetry(bool report_telemetry,
                                             ConfigOrigin origin) {
  set_telemetry(name(environment::DD_INSTRUMENTATION_TELEMETRY_ENABLED),
                std::to_string(report_telemetry), origin);
  config.report_telemetry = report_telemetry;
  return *this;
}

TracerConfig& TracerConfig::enable_128bit_trace_id(bool report_128bit_trace_id,
                                                   ConfigOrigin origin) {
  set_telemetry(name(environment::DD_TRACE_128_BIT_TRACEID_GENERATION_ENABLED),
                std::to_string(report_128bit_trace_id), origin);
  config.trace_id_128_bit = report_128bit_trace_id;
  return *this;
}

TracerConfig& TracerConfig::log_configuration_on_startup(bool log_on_startup,
                                                         ConfigOrigin origin) {
  set_telemetry(name(environment::DD_TRACE_STARTUP_LOGS),
                std::to_string(log_on_startup), origin);
  config.log_on_startup = log_on_startup;
  return *this;
}

TracerConfig& TracerConfig::set_injection_styles(
    std::vector<PropagationStyle> injection_styles, ConfigOrigin) {
  // set_telemetry(name(environment::DD_TRACE_PROPAGATION_STYLE_INJECT),
  //               stringutil::join(",", injection_styles), ConfigOrigin::CODE);
  config.injection_styles = std::move(injection_styles);
  return *this;
}

TracerConfig& TracerConfig::set_extraction_styles(
    std::vector<PropagationStyle> extraction_styles, ConfigOrigin) {
  // set_telemetry(name(environment::DD_TRACE_PROPAGATION_STYLE_INJECT),
  //               stringutil::join(",", extraction_styles),
  //               ConfigOrigin::CODE);
  config.extraction_styles = std::move(extraction_styles);
  return *this;
}

TracerConfig& TracerConfig::set_trace_sampler(TraceSamplerConfig trace_sampler,
                                              ConfigOrigin) {
  // set_telemetry(name(environment::DD_TRACE_SAMPLING_RULES),
  // trace_sampler_.to_json(),
  //               ConfigOrigin::CODE);
  trace_sampler_ = std::move(trace_sampler);
  return *this;
}

TracerConfig& TracerConfig::set_span_sampler(SpanSamplerConfig trace_sampler,
                                             ConfigOrigin) {
  // set_telemetry(name(environment::DD_TRACE_SAMPLING_RULES),
  // trace_sampler_.to_json(),
  //               ConfigOrigin::CODE);
  span_sampler_ = std::move(trace_sampler);
  return *this;
}

TracerConfig& TracerConfig::enable_hostname_in_span(bool report_hostname,
                                                    ConfigOrigin) {
  config.report_hostname = report_hostname;
  return *this;
}

TracerConfig& TracerConfig::set_http_client(
    std::shared_ptr<HTTPClient> http_client, ConfigOrigin) {
  agent_config.http_client = std::move(http_client);
  return *this;
}

TracerConfig& TracerConfig::set_event_scheduler(
    std::shared_ptr<EventScheduler> event_scheduler, ConfigOrigin) {
  agent_config.event_scheduler = std::move(event_scheduler);
  return *this;
}

TracerConfig& TracerConfig::set_flush_interval(
    std::chrono::milliseconds flush_interval, ConfigOrigin) {
  agent_config.flush_interval_milliseconds = flush_interval.count();
  return *this;
}

TracerConfig& TracerConfig::set_request_timeout(
    std::chrono::milliseconds request_timeout, ConfigOrigin) {
  agent_config.request_timeout_milliseconds = request_timeout.count();
  return *this;
}

TracerConfig& TracerConfig::set_shutdown_timeout(
    std::chrono::milliseconds shutdown_timeout, ConfigOrigin) {
  agent_config.shutdown_timeout_milliseconds = shutdown_timeout.count();
  return *this;
}

TracerConfig& TracerConfig::set_collector(std::shared_ptr<Collector> collector,
                                          ConfigOrigin) {
  config.collector = std::move(collector);
  return *this;
}

TracerConfig& TracerConfig::set_max_tags_header_size(
    std::size_t max_tags_header_size, ConfigOrigin) {
  config.tags_header_size = max_tags_header_size;
  return *this;
}

TracerConfig& TracerConfig::set_logger(std::shared_ptr<Logger> logger,
                                       ConfigOrigin) {
  config.logger = std::move(logger);
  return *this;
}

TracerConfig& TracerConfig::set_runtime_id(RuntimeID runtime_id, ConfigOrigin) {
  config.runtime_id = std::move(runtime_id);
  return *this;
}

TracerConfig& TracerConfig::set_remote_configuration_poll_interval(
    std::chrono::seconds poll_interval, ConfigOrigin) {
  agent_config.remote_configuration_poll_interval_seconds =
      poll_interval.count();
  return *this;
}

void TracerConfig::set_telemetry(StringView name, std::string value,
                                 ConfigOrigin origin) {
  ConfigTelemetry cfg_telemetry;
  cfg_telemetry.name = std::string{name};
  cfg_telemetry.value = value;
  cfg_telemetry.origin = origin;
  cfg_telemetry.error = nullopt;

  telemetry_data_[std::string{name}] = cfg_telemetry;
}

Expected<FinalizedTracerConfig> TracerConfig::finalize() {
  return finalize(default_clock);
}

Expected<FinalizedTracerConfig> TracerConfig::finalize(const Clock& clock) {
  FinalizedTracerConfig tmp(config);

  config.clock = clock;

  if (auto service_env = lookup(environment::DD_SERVICE)) {
    set_service_name(std::string{*service_env},
                     ConfigOrigin::ENVIRONMENT_VARIABLE);
  }

  if (config.defaults.service.empty()) {
    return Error{Error::SERVICE_NAME_REQUIRED, "Service name is required."};
  }

  if (auto environment_env = lookup(environment::DD_ENV)) {
    set_environment(std::string{*environment_env},
                    ConfigOrigin::ENVIRONMENT_VARIABLE);
  }

  if (auto version_env = lookup(environment::DD_VERSION)) {
    set_version(std::string{*version_env}, ConfigOrigin::ENVIRONMENT_VARIABLE);
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

    set_tags(std::move(*tags), ConfigOrigin::ENVIRONMENT_VARIABLE);
  }

  if (auto startup_env = lookup(environment::DD_TRACE_STARTUP_LOGS)) {
    log_configuration_on_startup(!falsy(*startup_env),
                                 ConfigOrigin::ENVIRONMENT_VARIABLE);
  }

  if (auto enabled_env = lookup(environment::DD_TRACE_ENABLED)) {
    enable_traces(!falsy(*enabled_env), ConfigOrigin::ENVIRONMENT_VARIABLE);
  }

  auto finalized = finalize_config(agent_config, config.logger, clock);
  if (auto* error = finalized.if_error()) {
    return std::move(*error);
  }
  config.agent_config = std::move(*finalized);

  if (auto enabled_env =
          lookup(environment::DD_INSTRUMENTATION_TELEMETRY_ENABLED)) {
    enable_telemetry(!falsy(*enabled_env), ConfigOrigin::ENVIRONMENT_VARIABLE);
  }

  if (auto trace_sampler_config = finalize_config(trace_sampler_)) {
    config.trace_sampler = std::move(*trace_sampler_config);
  } else {
    return std::move(trace_sampler_config.error());
  }

  if (auto span_sampler_config =
          finalize_config(span_sampler_, *config.logger)) {
    config.span_sampler = std::move(*span_sampler_config);
  } else {
    return std::move(span_sampler_config.error());
  }

  auto maybe_error =
      finalize_propagation_styles(config, config.extraction_styles,
                                  config.injection_styles, *config.logger);
  if (!maybe_error) {
    return maybe_error.error();
  }

  if (auto enabled_env =
          lookup(environment::DD_TRACE_128_BIT_TRACEID_GENERATION_ENABLED)) {
    enable_128bit_trace_id(!falsy(*enabled_env),
                           ConfigOrigin::ENVIRONMENT_VARIABLE);
  }

  // Set defaults if nullptr
  if (config.logger == nullptr) {
    config.logger = std::make_shared<CerrLogger>();
  }

  for (auto [_, value] : telemetry_data_) {
    config.telemetry_data.emplace_back(std::move(value));
  }

  std::swap(tmp, config);
  return tmp;
}

}  // namespace tracing
}  // namespace datadog
