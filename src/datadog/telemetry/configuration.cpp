#include <datadog/config.h>
#include <datadog/environment.h>
#include <datadog/stable_config.h>
#include <datadog/telemetry/configuration.h>
#include <datadog/version.h>

#include <utility>

#include "parse_util.h"

using namespace datadog::tracing;

namespace datadog::telemetry {

namespace {

tracing::Expected<Configuration> load_telemetry_env_config(
    const tracing::StableConfig* stable_config,
    const Configuration& user_config) {
  Configuration env_cfg;

  if (auto enabled_env = lookup(
          environment::DD_INSTRUMENTATION_TELEMETRY_ENABLED, stable_config)) {
    env_cfg.enabled = !falsy(*enabled_env);
  }

  if (auto metrics_enabled =
          lookup(environment::DD_TELEMETRY_METRICS_ENABLED, stable_config)) {
    env_cfg.report_metrics = !falsy(*metrics_enabled);
  }

  if (auto logs_enabled = lookup(
          environment::DD_TELEMETRY_LOG_COLLECTION_ENABLED, stable_config)) {
    env_cfg.report_logs = !falsy(*logs_enabled);
  }

  using Interval = tracing::Optional<double> Configuration::*;
  const std::pair<environment::Variable, Interval> intervals[] = {
      {environment::DD_TELEMETRY_METRICS_INTERVAL_SECONDS,
       &Configuration::metrics_interval_seconds},
      {environment::DD_TELEMETRY_HEARTBEAT_INTERVAL,
       &Configuration::heartbeat_interval_seconds},
      {environment::DD_TELEMETRY_EXTENDED_HEARTBEAT_INTERVAL,
       &Configuration::extended_heartbeat_interval_seconds},
  };
  for (const auto& [variable, field] : intervals) {
    const auto raw = lookup(variable, stable_config);
    if (!raw) continue;
    auto parsed = parse_double(*raw);
    if (auto error = parsed.if_error()) {
      if (!(stable_config && (user_config.*field))) return *error;
    } else {
      env_cfg.*field = *parsed;
    }
  }

  return env_cfg;
}

}  // namespace

tracing::Expected<FinalizedConfiguration> finalize_config(
    const Configuration& user_config) {
  return finalize_config(user_config, nullptr);
}

tracing::Expected<FinalizedConfiguration> finalize_config(
    const Configuration& user_config,
    const tracing::StableConfig* stable_config) {
  auto env_config = load_telemetry_env_config(stable_config, user_config);
  if (auto error = env_config.if_error()) {
    return *error;
  }

  FinalizedConfiguration result;
  const auto choose = [stable_config](const auto& from_env,
                                      const auto& from_code, auto fallback) {
    return choose_with_stable_config(from_env, from_code, fallback,
                                     stable_config);
  };

  // enabled
  result.enabled = choose(env_config->enabled, user_config.enabled, true);

  if (!result.enabled) {
    // NOTE(@dmehala): if the telemetry module is disabled then report metrics
    // is also disabled.
    result.report_metrics = false;
    result.report_logs = false;
  } else {
    // report_metrics
    result.report_metrics =
        choose(env_config->report_metrics, user_config.report_metrics, true);

    // report_logs
    result.report_logs =
        choose(env_config->report_logs, user_config.report_logs, true);
  }

  // debug
  if (auto enabled_debug_env =
          lookup(environment::DD_TELEMETRY_DEBUG, stable_config)) {
    result.debug = !falsy(*enabled_debug_env);
  } else {
    result.debug = false;
  }

  // metrics_interval_seconds
  const double metrics_interval =
      choose(env_config->metrics_interval_seconds,
             user_config.metrics_interval_seconds, 60.0);
  if (metrics_interval <= 0.) {
    return Error{Error::Code::OUT_OF_RANGE_INTEGER,
                 "Telemetry metrics polling interval must be a positive value"};
  }
  result.metrics_interval =
      std::chrono::duration_cast<std::chrono::milliseconds>(
          std::chrono::duration<double>(metrics_interval));

  // heartbeat_interval_seconds
  const double heartbeat_interval =
      choose(env_config->heartbeat_interval_seconds,
             user_config.heartbeat_interval_seconds, 10.0);
  if (heartbeat_interval <= 0.) {
    return Error{
        Error::Code::OUT_OF_RANGE_INTEGER,
        "Telemetry heartbeat polling interval must be a positive value"};
  }
  result.heartbeat_interval =
      std::chrono::duration_cast<std::chrono::milliseconds>(
          std::chrono::duration<double>(heartbeat_interval));

  // extended_heartbeat_interval_seconds
  const double extended_heartbeat_interval =
      choose(env_config->extended_heartbeat_interval_seconds,
             user_config.extended_heartbeat_interval_seconds, 86400.0);
  if (extended_heartbeat_interval <= 0) {
    return Error{
        Error::Code::OUT_OF_RANGE_INTEGER,
        "Telemetry extended heartbeat interval must be a positive value"};
  }
  result.extended_heartbeat_interval =
      std::chrono::duration_cast<std::chrono::milliseconds>(
          std::chrono::duration<double>(extended_heartbeat_interval));

  // integration_name
  result.integration_name =
      choose(env_config->integration_name, user_config.integration_name,
             std::string("datadog"));

  // integration_version
  result.integration_version =
      choose(env_config->integration_version, user_config.integration_version,
             tracing::tracer_version);

  // products
  result.products = user_config.products;

  // onboarding data
  if (auto install_id =
          lookup(environment::DD_INSTRUMENTATION_INSTALL_ID, stable_config)) {
    result.install_id = std::string(*install_id);
  }
  if (auto install_type =
          lookup(environment::DD_INSTRUMENTATION_INSTALL_TYPE, stable_config)) {
    result.install_type = std::string(*install_type);
  }
  if (auto install_time =
          lookup(environment::DD_INSTRUMENTATION_INSTALL_TIME, stable_config)) {
    result.install_time = std::string(*install_time);
  }

  return result;
}

}  // namespace datadog::telemetry
