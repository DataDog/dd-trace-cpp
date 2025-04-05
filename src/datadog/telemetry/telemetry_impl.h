#pragma once

#include <datadog/clock.h>
#include <datadog/config.h>
#include <datadog/event_scheduler.h>
#include <datadog/http_client.h>
#include <datadog/logger.h>
#include <datadog/telemetry/configuration.h>
#include <datadog/telemetry/metrics.h>
#include <datadog/tracer_signature.h>

#include "json.hpp"
#include "log.h"
#include "platform_util.h"

namespace datadog::telemetry {

/// The telemetry class is responsible for handling internal telemetry data to
/// track Datadog product usage. It _can_ collect and report logs and metrics.
class Telemetry final {
  DefaultMetrics metrics_;
  /// Configuration object containing the validated settings for telemetry
  FinalizedConfiguration config_;
  /// Shared pointer to the user logger instance.
  std::shared_ptr<tracing::Logger> logger_;
  std::vector<tracing::EventScheduler::Cancel> tasks_;
  tracing::HTTPClient::ResponseHandler telemetry_on_response_;
  tracing::HTTPClient::ErrorHandler telemetry_on_error_;
  tracing::HTTPClient::URL telemetry_endpoint_;
  tracing::TracerSignature tracer_signature_;
  std::shared_ptr<tracing::HTTPClient> http_client_;
  tracing::Clock clock_;
  std::shared_ptr<tracing::EventScheduler> scheduler_;

  // This uses a reference_wrapper so references to internal metric values can
  // be captured, and be iterated trivially when the values need to be
  // snapshotted and published in telemetry messages.
  using MetricSnapshot = std::vector<std::pair<std::time_t, uint64_t>>;
  std::vector<
      std::pair<std::reference_wrapper<telemetry::Metric>, MetricSnapshot>>
      metrics_snapshots_;
  std::vector<std::shared_ptr<telemetry::Metric>> user_metrics_;

  std::vector<tracing::ConfigMetadata> configuration_snapshot_;

  std::vector<telemetry::LogMessage> logs_;

  // Track sequence id per payload generated
  uint64_t seq_id_ = 0;
  // Track sequence id per configuration field
  std::unordered_map<tracing::ConfigName, std::size_t> config_seq_ids_;

  tracing::HostInfo host_info_;

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
  void log_error(std::string message, std::string stacktrace);

  /// capture and report internal warning message to Datadog.
  ///
  /// @param message The warning message to log.
  void log_warning(std::string message);

  void send_configuration_change();

  void capture_configuration_change(
      const std::vector<tracing::ConfigMetadata>& new_configuration);

 private:
  void send_telemetry(tracing::StringView request_type, std::string payload);

  void schedule_tasks();

  void capture_metrics();

  void log(std::string message, telemetry::LogLevel level,
           tracing::Optional<std::string> stacktrace = tracing::nullopt);

  nlohmann::json generate_telemetry_body(std::string request_type);
  nlohmann::json generate_configuration_field(
      const tracing::ConfigMetadata& config_metadata);

  // Constructs an `app-started` message using information provided when
  // constructed and the tracer_config value passed in.
  std::string app_started();
  // Constructs a messsage-batch containing `app-heartbeat`, and if metrics
  // have been modified, a `generate-metrics` message.
  std::string heartbeat_and_telemetry();
  // Constructs a message-batch containing `app-closing`, and if metrics have
  // been modified, a `generate-metrics` message.
  std::string app_closing();
};

}  // namespace datadog::telemetry
