#pragma once

#include <datadog/logger.h>
#include <datadog/telemetry/configuration.h>
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

 public:
  /// Constructor for the Telemetry class
  ///
  /// @param configuration The finalized configuration settings.
  /// @param logger User logger instance.
  /// @param metrics A vector user metrics to report.
  Telemetry(FinalizedConfiguration configuration,
            std::shared_ptr<tracing::Logger> logger,
            std::vector<std::shared_ptr<Metric>> metrics);

  ~Telemetry() = default;

  /// Capture and report internal error message to Datadog.
  ///
  /// @param message The error message.
  void log_error(std::string message);

  /// capture and report internal warning message to Datadog.
  ///
  /// @param message The warning message to log.
  void log_warning(std::string message);
};

}  // namespace telemetry
}  // namespace datadog
