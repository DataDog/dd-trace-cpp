#pragma once

#include <datadog/clock.h>
#include <datadog/config.h>
#include <datadog/event_scheduler.h>
#include <datadog/http_client.h>
#include <datadog/logger.h>
#include <datadog/telemetry/configuration.h>
#include <datadog/telemetry/metrics.h>
#include <datadog/tracer_signature.h>

#include <memory>
#include <unordered_map>
#include <vector>

namespace datadog {

namespace tracing {
class TracerTelemetry;
}  // namespace tracing

namespace telemetry {

/// The telemetry class is responsible for handling internal telemetry data to
/// track Datadog product usage. It _can_ collect and report logs and metrics.
///
/// IMPORTANT: This is intended for use only by Datadog Engineers.
class Telemetry final {
  // This structure contains all the metrics that are exposed by tracer
  // telemetry.
  struct {
    struct {
      telemetry::CounterMetric spans_created = {
          "spans_created", "tracers", {}, true};
      telemetry::CounterMetric spans_finished = {
          "spans_finished", "tracers", {}, true};

      telemetry::CounterMetric trace_segments_created_new = {
          "trace_segments_created", "tracers", {"new_continued:new"}, true};
      telemetry::CounterMetric trace_segments_created_continued = {
          "trace_segments_created",
          "tracers",
          {"new_continued:continued"},
          true};
      telemetry::CounterMetric trace_segments_closed = {
          "trace_segments_closed", "tracers", {}, true};
      telemetry::CounterMetric baggage_items_exceeded = {
          "context_header.truncated",
          "tracers",
          {{"truncation_reason:baggage_item_count_exceeded"}},
          true,
      };
      telemetry::CounterMetric baggage_bytes_exceeded = {
          "context_header.truncated",
          "tracers",
          {{"truncation_reason:baggage_byte_count_exceeded"}},
          true,
      };
    } tracer;
    struct {
      telemetry::CounterMetric requests = {
          "trace_api.requests", "tracers", {}, true};

      telemetry::CounterMetric responses_1xx = {
          "trace_api.responses", "tracers", {"status_code:1xx"}, true};
      telemetry::CounterMetric responses_2xx = {
          "trace_api.responses", "tracers", {"status_code:2xx"}, true};
      telemetry::CounterMetric responses_3xx = {
          "trace_api.responses", "tracers", {"status_code:3xx"}, true};
      telemetry::CounterMetric responses_4xx = {
          "trace_api.responses", "tracers", {"status_code:4xx"}, true};
      telemetry::CounterMetric responses_5xx = {
          "trace_api.responses", "tracers", {"status_code:5xx"}, true};

      telemetry::CounterMetric errors_timeout = {
          "trace_api.errors", "tracers", {"type:timeout"}, true};
      telemetry::CounterMetric errors_network = {
          "trace_api.errors", "tracers", {"type:network"}, true};
      telemetry::CounterMetric errors_status_code = {
          "trace_api.errors", "tracers", {"type:status_code"}, true};

    } trace_api;
  } metrics_;

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
            tracing::EventScheduler& scheduler,
            tracing::HTTPClient::URL agent_url,
            tracing::Clock clock = tracing::default_clock);

  /// Destructor
  ///
  /// Send last metrics snapshot and `app-closing` event.
  ~Telemetry();

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
};

}  // namespace telemetry
}  // namespace datadog
