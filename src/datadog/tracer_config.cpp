#include <datadog/environment.h>
#include <datadog/string_view.h>
#include <datadog/tracer_config.h>

#include <algorithm>
#include <cassert>
#include <string>
#include <unordered_map>
#include <vector>

#include "datadog/optional.h"
#include "datadog_agent.h"
#include "json.hpp"
#include "null_logger.h"
#include "parse_util.h"
#include "platform_util.h"
#include "string_util.h"
#include "threaded_event_scheduler.h"

namespace datadog {
namespace tracing {
namespace {

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
    message += std::string(to_string_view(styles.back()));
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
          lookup(environment::DD_TRACE_128_BIT_TRACEID_GENERATION_ENABLED)) {
    env_cfg.generate_128bit_trace_ids = !falsy(*enabled_env);
  }

  if (auto apm_enabled_env = lookup(environment::DD_APM_TRACING_ENABLED)) {
    env_cfg.tracing_enabled = !falsy(*apm_enabled_env);
  }

  if (auto resource_renaming_enabled_env =
          lookup(environment::DD_TRACE_RESOURCE_RENAMING_ENABLED)) {
    env_cfg.resource_renaming_enabled = !falsy(*resource_renaming_enabled_env);
  }
  if (auto resource_renaming_always_simplified_endpoint_env = lookup(
          environment::DD_TRACE_RESOURCE_RENAMING_ALWAYS_SIMPLIFIED_ENDPOINT)) {
    env_cfg.resource_renaming_always_simplified_endpoint =
        !falsy(*resource_renaming_always_simplified_endpoint_env);
  }

  // Baggage
  if (auto baggage_items_env =
          lookup(environment::DD_TRACE_BAGGAGE_MAX_ITEMS)) {
    auto maybe_value = parse_uint64(*baggage_items_env, 10);
    if (auto *error = maybe_value.if_error()) {
      return *error;
    }

    env_cfg.baggage_max_items = std::move(*maybe_value);
  }

  if (auto baggage_bytes_env =
          lookup(environment::DD_TRACE_BAGGAGE_MAX_BYTES)) {
    auto maybe_value = parse_uint64(*baggage_bytes_env, 10);
    if (auto *error = maybe_value.if_error()) {
      return *error;
    }

    env_cfg.baggage_max_bytes = std::move(*maybe_value);
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

    const auto var_name = name(var);
    const auto var_name_override = name(var_override);

    logger.log_error(Error{
        Error::MULTIPLE_PROPAGATION_STYLE_ENVIRONMENT_VARIABLES,
        warn_message(var_name, *value, var_name_override, *value_override)});
  }

  try {
    const auto global_styles =
        styles_from_env(environment::DD_TRACE_PROPAGATION_STYLE);

    if (auto trace_extraction_styles =
            styles_from_env(environment::DD_TRACE_PROPAGATION_STYLE_EXTRACT)) {
      env_cfg.extraction_styles = std::move(*trace_extraction_styles);
    } else if (auto extraction_styles =
                   styles_from_env(environment::DD_PROPAGATION_STYLE_EXTRACT)) {
      env_cfg.extraction_styles = std::move(*extraction_styles);
    } else {
      env_cfg.extraction_styles = global_styles;
    }

    if (auto trace_injection_styles =
            styles_from_env(environment::DD_TRACE_PROPAGATION_STYLE_INJECT)) {
      env_cfg.injection_styles = std::move(*trace_injection_styles);
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
      user_config.logger ? user_config.logger : std::make_shared<NullLogger>();

  Expected<TracerConfig> env_config = load_tracer_env_config(*logger);
  if (auto error = env_config.if_error()) {
    return *error;
  }

  FinalizedTracerConfig final_config;
  final_config.clock = clock;
  final_config.logger = logger;

  std::unordered_map<ConfigName, std::vector<ConfigMetadata>>
      all_sources_configs;

  // DD_SERVICE
  final_config.defaults.service = resolve_and_record_config(
      env_config->service, user_config.service, &all_sources_configs,
      &final_config.metadata, ConfigName::SERVICE_NAME, get_process_name());

  // Service type
  final_config.defaults.service_type =
      value_or(env_config->service_type, user_config.service_type, "web");

  // DD_ENV
  final_config.defaults.environment = resolve_and_record_config(
      env_config->environment, user_config.environment, &all_sources_configs,
      &final_config.metadata, ConfigName::SERVICE_ENV);

  // DD_VERSION
  final_config.defaults.version = resolve_and_record_config(
      env_config->version, user_config.version, &all_sources_configs,
      &final_config.metadata, ConfigName::SERVICE_VERSION);

  // Span name
  final_config.defaults.name = value_or(env_config->name, user_config.name, "");

  // DD_TAGS
  final_config.defaults.tags = resolve_and_record_config(
      env_config->tags, user_config.tags, &all_sources_configs,
      &final_config.metadata, ConfigName::TAGS,
      std::unordered_map<std::string, std::string>{},
      [](const auto &tags) { return join_tags(tags); });

  // Extraction Styles
  const std::vector<PropagationStyle> default_propagation_styles{
      PropagationStyle::DATADOG, PropagationStyle::W3C,
      PropagationStyle::BAGGAGE};

  final_config.extraction_styles = resolve_and_record_config(
      env_config->extraction_styles, user_config.extraction_styles,
      &all_sources_configs, &final_config.metadata,
      ConfigName::EXTRACTION_STYLES, default_propagation_styles,
      [](const std::vector<PropagationStyle> &styles) {
        return join_propagation_styles(styles);
      });

  if (final_config.extraction_styles.empty()) {
    return Error{Error::MISSING_SPAN_EXTRACTION_STYLE,
                 "At least one extraction style must be specified."};
  }

  // Injection Styles
  final_config.injection_styles = resolve_and_record_config(
      env_config->injection_styles, user_config.injection_styles,
      &all_sources_configs, &final_config.metadata,
      ConfigName::INJECTION_STYLES, default_propagation_styles,
      [](const std::vector<PropagationStyle> &styles) {
        return join_propagation_styles(styles);
      });

  if (final_config.injection_styles.empty()) {
    return Error{Error::MISSING_SPAN_INJECTION_STYLE,
                 "At least one injection style must be specified."};
  }

  // Startup Logs
  final_config.log_on_startup = resolve_and_record_config(
      env_config->log_on_startup, user_config.log_on_startup,
      &all_sources_configs, &final_config.metadata, ConfigName::STARTUP_LOGS,
      true, [](const bool &b) { return to_string(b); });

  // Report traces
  final_config.report_traces = resolve_and_record_config(
      env_config->report_traces, user_config.report_traces,
      &all_sources_configs, &final_config.metadata, ConfigName::REPORT_TRACES,
      true, [](const bool &b) { return to_string(b); });

  // Report hostname
  final_config.report_hostname =
      value_or(env_config->report_hostname, user_config.report_hostname, false);

  // Tags Header Size
  final_config.tags_header_size = value_or(
      env_config->max_tags_header_size, user_config.max_tags_header_size, 512);

  // 128b Trace IDs
  final_config.generate_128bit_trace_ids = resolve_and_record_config(
      env_config->generate_128bit_trace_ids,
      user_config.generate_128bit_trace_ids, &all_sources_configs,
      &final_config.metadata, ConfigName::GENEREATE_128BIT_TRACE_IDS, true,
      [](const bool &b) { return to_string(b); });

  // Integration name & version
  final_config.integration_name = value_or(
      env_config->integration_name, user_config.integration_name, "datadog");
  final_config.integration_version =
      value_or(env_config->integration_version, user_config.integration_version,
               tracer_version);

  // Baggage - max items
  final_config.baggage_opts.max_items = resolve_and_record_config(
      env_config->baggage_max_items, user_config.baggage_max_items,
      &all_sources_configs, &final_config.metadata,
      ConfigName::TRACE_BAGGAGE_MAX_ITEMS, 64UL,
      [](const size_t &i) { return std::to_string(i); });

  // Baggage - max bytes
  final_config.baggage_opts.max_bytes = resolve_and_record_config(
      env_config->baggage_max_bytes, user_config.baggage_max_bytes,
      &all_sources_configs, &final_config.metadata,
      ConfigName::TRACE_BAGGAGE_MAX_BYTES, 8192UL,
      [](const size_t &i) { return std::to_string(i); });

  if (final_config.baggage_opts.max_items <= 0 ||
      final_config.baggage_opts.max_bytes < 3) {
    auto it = std::remove(final_config.extraction_styles.begin(),
                          final_config.extraction_styles.end(),
                          PropagationStyle::BAGGAGE);
    final_config.extraction_styles.erase(it);

    it = std::remove(final_config.injection_styles.begin(),
                     final_config.injection_styles.end(),
                     PropagationStyle::BAGGAGE);
    final_config.injection_styles.erase(it);
  }

  if (user_config.runtime_id) {
    final_config.runtime_id = user_config.runtime_id;
  }

  final_config.process_tags = user_config.process_tags;

  auto agent_finalized =
      finalize_config(user_config.agent, final_config.logger, clock);
  if (auto *error = agent_finalized.if_error()) {
    return std::move(*error);
  }

  if (auto trace_sampler_config =
          finalize_config(user_config.trace_sampler, &all_sources_configs)) {
    final_config.metadata.merge(trace_sampler_config->metadata);
    final_config.trace_sampler = std::move(*trace_sampler_config);
  } else {
    return std::move(trace_sampler_config.error());
  }

  if (auto span_sampler_config =
          finalize_config(user_config.span_sampler, *logger)) {
    final_config.metadata.merge(span_sampler_config->metadata);
    final_config.span_sampler = std::move(*span_sampler_config);
  } else {
    return std::move(span_sampler_config.error());
  }

  // agent url
  final_config.agent_url = agent_finalized->url;

  if (user_config.event_scheduler == nullptr) {
    final_config.event_scheduler = std::make_shared<ThreadedEventScheduler>();
  } else {
    final_config.event_scheduler = user_config.event_scheduler;
  }

  final_config.http_client = agent_finalized->http_client;

  // telemetry
  if (auto telemetry_final_config =
          telemetry::finalize_config(user_config.telemetry)) {
    final_config.telemetry = std::move(*telemetry_final_config);
    final_config.telemetry.products.emplace_back(telemetry::Product{
        telemetry::Product::Name::tracing, true, tracer_version, nullopt,
        nullopt, all_sources_configs});
  } else {
    return std::move(telemetry_final_config.error());
  }

  // APM Tracing Enabled
  final_config.tracing_enabled = resolve_and_record_config(
      env_config->tracing_enabled, user_config.tracing_enabled,
      &all_sources_configs, &final_config.metadata,
      ConfigName::APM_TRACING_ENABLED, true,
      [](const bool &b) { return to_string(b); });

  {
    // Resource Renaming Enabled
    const bool resource_renaming_enabled = resolve_and_record_config(
        env_config->resource_renaming_enabled,
        user_config.resource_renaming_enabled, &all_sources_configs,
        &final_config.metadata, ConfigName::TRACE_RESOURCE_RENAMING_ENABLED,
        false, [](const bool &b) { return to_string(b); });

    // Resource Renaming Always Simplified Endpoint
    const bool resource_renaming_always_simplified_endpoint =
        resolve_and_record_config(
            env_config->resource_renaming_always_simplified_endpoint,
            user_config.resource_renaming_always_simplified_endpoint,
            &all_sources_configs, &final_config.metadata,
            ConfigName::TRACE_RESOURCE_RENAMING_ALWAYS_SIMPLIFIED_ENDPOINT,
            false, [](const bool &b) { return to_string(b); });

    if (!resource_renaming_enabled) {
      final_config.resource_renaming_mode =
          HttpEndpointCalculationMode::DISABLED;
    } else if (resource_renaming_always_simplified_endpoint) {
      final_config.resource_renaming_mode =
          HttpEndpointCalculationMode::ALWAYS_CALCULATE;
    } else {
      final_config.resource_renaming_mode =
          HttpEndpointCalculationMode::FALLBACK;
    }
  }

  // Whether APM tracing is enabled. This affects whether the
  // "Datadog-Client-Computed-Stats: yes" header is sent with trace requests.
  if (!final_config.tracing_enabled) {
    agent_finalized->stats_computation_enabled = !final_config.tracing_enabled;

    // Overwrite the trace sampler configuration with a specific trace sampler
    // configuration which:
    //   - always keep spans generated by other products;
    //   - allow one trace per minute for service liveness;
    final_config.trace_sampler =
        FinalizedTraceSamplerConfig::apm_tracing_disabled_config();
  }

  if (!user_config.collector) {
    final_config.collector = *agent_finalized;
    final_config.metadata.merge(agent_finalized->metadata);
  } else {
    final_config.collector = user_config.collector;
  }

  return final_config;
}

}  // namespace tracing
}  // namespace datadog
