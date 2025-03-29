#pragma once

#include <datadog/clock.h>
#include <datadog/config.h>
#include <datadog/event_scheduler.h>
#include <datadog/http_client.h>
#include <datadog/logger.h>
#include <datadog/telemetry/configuration.h>
#include <datadog/telemetry/metrics.h>

#include <memory>
#include <unordered_map>
#include <vector>

/// Telemetry functions are responsibles for handling internal telemetry data to
/// track Datadog product usage. It _can_ collect and report logs and metrics.
///
/// IMPORTANT: This is intended for use only by Datadog Engineers.
namespace datadog::telemetry {

/// Initialize the telemetry module
/// Once initialized, the telemetry module is running for the entier lifecycle
/// of the application.
///
/// @param configuration The finalized configuration settings.
/// @param logger User logger instance.
/// @param metrics A vector user metrics to report.
///
/// NOTE: Make sure to call `init` before calling any of the other telemetry
/// functions.
void init(FinalizedConfiguration configuration,
          std::shared_ptr<tracing::Logger> logger,
          std::shared_ptr<tracing::HTTPClient> client,
          std::vector<std::shared_ptr<Metric>> metrics,
          std::shared_ptr<tracing::EventScheduler> event_scheduler,
          tracing::HTTPClient::URL agent_url,
          tracing::Clock clock = tracing::default_clock);

/// Sends a notification indicating that the application has started.
///
/// This function is responsible for reporting the application has successfully
/// started. It takes a configuration map as a parameter, which contains various
/// configuration settings helping to understand how our product are used.
///
/// @param conf A map containing configuration names and their corresponding
/// metadata.
///
/// @note This function should be called after the application has completed its
/// initialization process to ensure that all components are aware of the
/// application's startup status.
void send_app_started(const std::unordered_map<tracing::ConfigName,
                                               tracing::ConfigMetadata>& conf);

/// Sends configuration changes.
///
/// This function is responsible for sending reported configuration changes
/// reported by `capture_configuration_change`.
///
/// @note This function should be called _AFTER_ all configuration changes are
/// captures by `capture_configuration_change`.
void send_configuration_change();

/// Captures a change in the application's configuration.
///
/// This function is called to report updates to the application's
/// configuration. It takes a vector of new configuration metadata as a
/// parameter, which contains the updated settings.
///
/// @param new_configuration A vector containing the new configuration metadata.
///
/// @note This function should be invoked whenever there is a change in the
/// configuration.
void capture_configuration_change(
    const std::vector<tracing::ConfigMetadata>& new_configuration);

/// Provides access to the telemetry metrics for updating the values.
/// This value should not be stored.
DefaultMetrics& metrics();

/// Report internal warning message to Datadog.
///
/// @param message The warning message to log.
void report_warning_log(std::string message);

/// Report internal error message to Datadog.
///
/// @param message The error message.
void report_error_log(std::string message);

}  // namespace datadog::telemetry
