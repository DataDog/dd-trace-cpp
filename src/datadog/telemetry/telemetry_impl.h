#pragma once

#include <datadog/clock.h>
#include <datadog/config.h>
#include <datadog/event_scheduler.h>
#include <datadog/http_client.h>
#include <datadog/logger.h>
#include <datadog/telemetry/configuration.h>
#include <datadog/telemetry/metric_handle.h>
#include <datadog/telemetry/metrics.h>
#include <datadog/tracer_signature.h>

#include <memory>
#include <mutex>

#include "json.hpp"
#include "log.h"
#include "metric_context.h"
#include "platform_util.h"
#include "status_tags.h"

namespace datadog::telemetry {

using MetricSnapshot = std::vector<std::pair<std::time_t, uint64_t>>;

// The `level` tag of the logs_created metric.
struct LogTags {
  LogLevel level = LogLevel::ERROR;

  static auto tag_fields() { return std::tuple{tag("level", &LogTags::level)}; }
};

// The `Telemetry` class is responsible for handling internal telemetry data to
// track Datadog product usage. It _can_ collect and report logs and metrics.
//
// The current implementation can lead a significant amount of overhead if the
// mutex is highly disputed. Unless this is proven to be indeed a bottleneck, we
// embrace KISS principle. However, in a future iteration we could use multiple
// producers - single consumer queue or lock-free queue.
class Telemetry final : public std::enable_shared_from_this<Telemetry> {
  // Configuration object containing the validated settings for telemetry
  FinalizedConfiguration config_;
  // Shared pointer to the user logger instance
  std::shared_ptr<tracing::Logger> logger_;
  std::vector<tracing::EventScheduler::Cancel> tasks_;
  tracing::HTTPClient::URL telemetry_endpoint_;
  tracing::TracerSignature tracer_signature_;
  std::shared_ptr<tracing::HTTPClient> http_client_;
  tracing::Clock clock_;
  std::shared_ptr<tracing::EventScheduler> scheduler_;

  // Counter
  std::mutex counter_mutex_;
  std::unordered_map<MetricContext<Counter>, MetricSnapshot> counters_snapshot_;

  // Rate
  std::mutex rate_mutex_;
  std::unordered_map<MetricContext<Rate>, MetricSnapshot> rates_snapshot_;

  // The metric handles created with this Telemetry.
  details::MetricRegistry metrics_;

  // The metrics of this Telemetry itself.
  std::shared_ptr<CounterHandle<LogTags>> logs_created_;
  std::shared_ptr<CounterHandle<>> api_requests_;
  std::shared_ptr<CounterHandle<StatusTags>> api_responses_;
  std::shared_ptr<CounterHandle<>> api_errors_;
  std::shared_ptr<DistributionHandle<>> api_bytes_sent_;

  // Configuration
  std::vector<tracing::ConfigMetadata> configuration_snapshot_;

  // Product
  std::mutex product_changes_mutex_;
  std::unordered_map<Product::Name, Product> product_changes_;

  std::mutex log_mutex_;
  std::vector<telemetry::LogMessage> logs_;

  // Track sequence id per payload generated
  uint64_t seq_id_ = 0;
  // Track sequence id per configuration field
  std::unordered_map<tracing::ConfigName, std::size_t> config_seq_ids_;
  // Track the latest reported value for each configuration field
  std::unordered_map<tracing::ConfigName, tracing::ConfigMetadata>
      all_configurations_;

  tracing::HostInfo host_info_;

  std::mutex http_client_mutex_;

 public:
  static std::shared_ptr<Telemetry> create(
      FinalizedConfiguration configuration,
      tracing::TracerSignature tracer_signature,
      std::shared_ptr<tracing::Logger> logger,
      std::shared_ptr<tracing::HTTPClient> client,
      std::shared_ptr<tracing::EventScheduler> event_scheduler,
      tracing::HTTPClient::URL agent_url,
      tracing::Clock clock = tracing::default_clock);

  ~Telemetry();

 private:
  Telemetry(FinalizedConfiguration configuration,
            tracing::TracerSignature tracer_signature,
            std::shared_ptr<tracing::Logger> logger,
            std::shared_ptr<tracing::HTTPClient> client,
            std::shared_ptr<tracing::EventScheduler> event_scheduler,
            tracing::HTTPClient::URL agent_url,
            tracing::Clock clock = tracing::default_clock);

 public:
  // Capture and report internal error message to Datadog.
  void log_error(std::string message);
  void log_error(std::string message, std::string stacktrace);

  // Capture and report internal warning message to Datadog.
  void log_warning(std::string message);

  void send_configuration_change();

  void capture_configuration_change(
      const std::vector<tracing::ConfigMetadata>& new_configuration);

  void capture_product_change(const Product& product);

  // Deterministic shutdown: cancels scheduled tasks, sends the app-closing
  // payload, drains in-flight HTTP requests, and releases the HTTP client
  // (joining the background thread if this is the last reference).
  // After this call the Telemetry object is inert and safe to destroy.
  void shutdown();

  // Creates the metric handles that this Telemetry sends.
  details::MetricRegistry& metrics() { return metrics_; }

 private:
  void app_started();
  void app_closing();

  void send_payload(tracing::StringView request_type, std::string payload);

  void schedule_tasks();

  void capture_metrics();

  void log(std::string message, telemetry::LogLevel level,
           tracing::Optional<std::string> stacktrace = tracing::nullopt);

  void append_product_change(nlohmann::json& batch_payloads);

  nlohmann::json generate_telemetry_body(std::string request_type);
  nlohmann::json serialize_configuration_field(
      const tracing::ConfigMetadata& config_metadata, std::size_t seq_id);
  nlohmann::json generate_configuration_field(
      const tracing::ConfigMetadata& config_metadata);

  // Constructs an `app-started` message using information provided when
  // constructed and the tracer_config value passed in.
  std::string app_started_payload();
  // Constructs a messsage-batch containing `app-heartbeat`, and if metrics
  // have been modified, a `generate-metrics` message.
  std::string heartbeat_and_telemetry();
  // Constructs a message-batch containing `app-extended-heartbeat` with the
  // full configuration payload.
  std::string extended_heartbeat_payload();
  // Constructs a message-batch containing `app-closing`, and if metrics have
  // been modified, a `generate-metrics` message.
  std::string app_closing_payload();
};

}  // namespace datadog::telemetry
