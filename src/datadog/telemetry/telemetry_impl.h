#pragma once

#include <datadog/event_scheduler.h>
#include <datadog/http_client.h>
#include <datadog/logger.h>
#include <datadog/telemetry/configuration.h>

#include "tracer_telemetry.h"

namespace datadog::telemetry {

/// The telemetry class is responsible for handling internal telemetry data to
/// track Datadog product usage. It _can_ collect and report logs and metrics.
class Telemetry final {
  DefaultMetrics metrics_;
  /// Configuration object containing the validated settings for telemetry
  FinalizedConfiguration config_;
  /// Shared pointer to the user logger instance.
  std::shared_ptr<tracing::Logger> logger_;
  std::shared_ptr<tracing::TracerTelemetry> tracer_telemetry_;
  std::vector<tracing::EventScheduler::Cancel> tasks_;
  tracing::HTTPClient::ResponseHandler telemetry_on_response_;
  tracing::HTTPClient::ErrorHandler telemetry_on_error_;
  tracing::HTTPClient::URL telemetry_endpoint_;
  tracing::TracerSignature tracer_signature_;
  std::shared_ptr<tracing::HTTPClient> http_client_;
  tracing::Clock clock_;
  std::shared_ptr<tracing::EventScheduler> scheduler_;

 public:
  /// Constructor for the Telemetry class
  ///
  /// @param configuration The finalized configuration settings.
  /// @param logger User logger instance.
  /// @param metrics A vector user metrics to report.
  Telemetry(FinalizedConfiguration configuration,
            std::shared_ptr<tracing::Logger> logger,
            std::shared_ptr<tracing::HTTPClient> client,
            std::vector<std::shared_ptr<Metric>> metrics,
            std::shared_ptr<tracing::EventScheduler> event_scheduler,
            tracing::HTTPClient::URL agent_url,
            tracing::Clock clock = tracing::default_clock);

  /// Destructor
  ///
  /// Send last metrics snapshot and `app-closing` event.
  ~Telemetry();

  /// Move semantics.
  Telemetry(Telemetry&& rhs);
  Telemetry& operator=(Telemetry&&);

  // Provides access to the telemetry metrics for updating the values.
  // This value should not be stored.
  inline auto& metrics() { return metrics_; }

  /// Capture and report internal error message to Datadog.
  ///
  /// @param message The error message.
  void log_error(std::string message);

  /// capture and report internal warning message to Datadog.
  ///
  /// @param message The warning message to log.
  void log_warning(std::string message);

  void send_app_started(
      const std::unordered_map<tracing::ConfigName, tracing::ConfigMetadata>&
          config_metadata);

  void send_configuration_change();

  void capture_configuration_change(
      const std::vector<tracing::ConfigMetadata>& new_configuration);

  void send_app_closing();

 private:
  void send_telemetry(tracing::StringView request_type, std::string payload);

  void send_heartbeat_and_telemetry();
  void schedule_tasks();
};

}  // namespace datadog::telemetry
