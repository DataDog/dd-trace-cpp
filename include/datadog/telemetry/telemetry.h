#pragma once

#include <datadog/event_scheduler.h>
#include <datadog/expected.h>
#include <datadog/http_client.h>
#include <datadog/logger.h>
#include <datadog/telemetry/configuration.h>
#include <datadog/telemetry/log.h>
#include <datadog/telemetry/metrics.h>

#include <memory>
#include <vector>

namespace datadog {

namespace tracing {
class DatadogAgent;
class TracerTelemetry;
}  // namespace tracing

namespace telemetry {

/// The telemetry class is responsible for handling internal telemetry data to
/// track Datadog product usage. It _can_ collect and report logs and metrics.
///
/// IMPORTANT: This is intended for use only by Datadog Engineers.
class Telemetry final {
  /// Configuration object containing the validated settings for telemetry
  FinalizedConfiguration config_;
  /// Shared pointer to the user logger instance.
  std::shared_ptr<tracing::Logger> logger_;
  /// TODO(@dmehala): Legacy dependency.
  std::shared_ptr<tracing::DatadogAgent> datadog_agent_;
  std::shared_ptr<tracing::TracerTelemetry> tracer_telemetry_;

  tracing::EventScheduler::Cancel task_;
  std::shared_ptr<tracing::HTTPClient> http_client_;

  std::vector<telemetry::LogMessage> logs_;

 public:
  /// Constructor for the Telemetry class
  ///
  /// @param configuration The finalized configuration settings.
  /// @param logger User logger instance.
  /// @param metrics A vector user metrics to report.
  Telemetry(FinalizedConfiguration configuration,
            std::shared_ptr<tracing::Logger> logger,
            std::vector<std::shared_ptr<Metric>> metrics);

  Telemetry(FinalizedConfiguration configuration,
            tracing::EventScheduler& scheduler,
            std::shared_ptr<tracing::HTTPClient> http_client);

  ~Telemetry();

  /// Capture and report internal error message to Datadog.
  ///
  /// @param message The error message.
  void log_error(std::string message);

  /// capture and report internal warning message to Datadog.
  ///
  /// @param message The warning message to log.
  void log_warning(std::string message);

 private:
  /// Take a snapshot of all the metrics collected.
  void snapshot_metrics();
  /// The thing actually running in a thread.
  void flush();
};

}  // namespace telemetry
}  // namespace datadog
