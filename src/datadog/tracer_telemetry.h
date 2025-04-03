#pragma once

// This component provides a class, TracerTelemetry, that is used to collect
// data from the activity of the tracer implementation, and encode messages that
// can be submitted to the Datadog Agent.
//
// Counter metrics are updated in other parts of the tracers, with the values
// being managed by this class.
//
// The messages that TracerTelemetry produces are
// - `app-started`
// - `message-batch`
// - `app-heartbeat`
// - `generate-metrics`
// - `app-closing`
// - `app-client-configuration-change`
//
// `app-started` messages are sent as part of initializing the tracer.
//
// At 60 second intervals, a `message-batch` message is sent containing an
// `app-heartbeat` message, and if metrics have changed during that interval, a
// `generate-metrics` message is also included in the batch.
//
// `app-closing` messages are sent as part of terminating the tracer. These are
// sent as a `message-batch` message , and if metrics have changed since the
// last `app-heartbeat` event, a `generate-metrics` message is also included in
// the batch.
//
// `app-client-configuration-change` messages are sent as soon as the tracer
// configuration has been updated by a Remote Configuration event.
#include <datadog/clock.h>
#include <datadog/config.h>
#include <datadog/runtime_id.h>
#include <datadog/telemetry/metrics.h>
#include <datadog/tracer_signature.h>

#include <vector>

#include "json.hpp"
#include "platform_util.h"
#include "telemetry/log.h"

namespace datadog {
namespace tracing {

class Logger;
struct SpanDefaults;

class TracerTelemetry {
  bool enabled_ = false;
  bool debug_ = true;
  Clock clock_;
  std::shared_ptr<Logger> logger_;
  HostInfo host_info_;
  TracerSignature tracer_signature_;
  std::string integration_name_;
  std::string integration_version_;
  // Track sequence id per payload generated
  uint64_t seq_id_ = 0;
  // Track sequence id per configuration field
  std::unordered_map<ConfigName, std::size_t> config_seq_ids;
  // Each metric has an associated MetricSnapshot that contains the data points,
  // represented as a timestamp and the value of that metric.
  using MetricSnapshot = std::vector<std::pair<std::time_t, uint64_t>>;
  // This uses a reference_wrapper so references to internal metric values can
  // be captured, and be iterated trivially when the values need to be
  // snapshotted and published in telemetry messages.
  std::vector<
      std::pair<std::reference_wrapper<telemetry::Metric>, MetricSnapshot>>
      metrics_snapshots_;

  std::vector<ConfigMetadata> configuration_snapshot_;

  nlohmann::json generate_telemetry_body(std::string request_type);

  nlohmann::json generate_configuration_field(
      const ConfigMetadata& config_metadata);

  std::vector<std::shared_ptr<telemetry::Metric>> user_metrics_;

  std::vector<telemetry::LogMessage> logs_;

 public:
  TracerTelemetry(
      bool enabled, const Clock& clock, const std::shared_ptr<Logger>& logger,
      const TracerSignature& tracer_signature,
      const std::string& integration_name,
      const std::string& integration_version,
      const std::vector<std::reference_wrapper<telemetry::Metric>>&
          internal_metrics,
      const std::vector<std::shared_ptr<telemetry::Metric>>& user_metrics =
          std::vector<std::shared_ptr<telemetry::Metric>>{});
  inline bool enabled() { return enabled_; }
  inline bool debug() { return debug_; }
  // Constructs an `app-started` message using information provided when
  // constructed and the tracer_config value passed in.
  std::string app_started(
      const std::unordered_map<ConfigName, ConfigMetadata>& configurations);
  // This is used to take a snapshot of the current state of metrics and
  // collect timestamped "points" of values. These values are later submitted
  // in `generate-metrics` messages.
  void capture_metrics();
  void capture_configuration_change(
      const std::vector<ConfigMetadata>& new_configuration);
  // Constructs a messsage-batch containing `app-heartbeat`, and if metrics
  // have been modified, a `generate-metrics` message.
  std::string heartbeat_and_telemetry();
  // Constructs a message-batch containing `app-closing`, and if metrics have
  // been modified, a `generate-metrics` message.
  std::string app_closing();
  // Construct an `app-client-configuration-change` message.
  Optional<std::string> configuration_change();

  inline void log(std::string message, telemetry::LogLevel level,
                  Optional<std::string> stacktrace = nullopt) {
    auto timestamp = std::chrono::duration_cast<std::chrono::seconds>(
                         clock_().wall.time_since_epoch())
                         .count();
    logs_.emplace_back(telemetry::LogMessage{std::move(message), level,
                                             stacktrace, timestamp});
  }
};

}  // namespace tracing
}  // namespace datadog
