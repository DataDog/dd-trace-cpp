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
#include <vector>

/// Telemetry functions are responsibles for handling internal telemetry data to
/// track Datadog product usage. It _can_ collect and report logs and metrics.
///
/// IMPORTANT: This is intended for use only by Datadog Engineers.
namespace datadog::telemetry {

/// Initialize the telemetry module
/// Once initialized, sends a notification indicating that the application has
/// started. The telemetry module is running for the entire lifecycle of the
/// application.
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
          std::shared_ptr<tracing::EventScheduler> event_scheduler,
          tracing::HTTPClient::URL agent_url,
          tracing::Clock clock = tracing::default_clock);

void init(FinalizedConfiguration configuration,
          tracing::TracerSignature tracer_signature,
          std::shared_ptr<tracing::Logger> logger,
          std::shared_ptr<tracing::HTTPClient> client,
          std::shared_ptr<tracing::EventScheduler> event_scheduler,
          tracing::HTTPClient::URL agent_url,
          tracing::Clock clock = tracing::default_clock);

/// Deterministically shut down the telemetry module.
///
/// Cancels all scheduled tasks, sends the app-closing payload, and waits up
/// to 2 seconds for in-flight requests to complete (HTTPClient::drain()).
/// Then releases the HTTP client reference; if this is the last shared_ptr
/// to the client, the background Curl thread is joined synchronously and any
/// remaining in-flight requests are abandoned without firing their callbacks.
/// If other holders of the client remain, the Curl thread continues running
/// and requests may still complete and fire their callbacks after this call
/// returns (the callbacks guard against this with weak_from_this()). After
/// this call all telemetry send functions become no-ops. Safe to call even if
/// telemetry was never initialized, but must be called at most once after a
/// successful init.
///
/// Call this from the worker process exit path (e.g. before destroying the
/// tracer) so that telemetry's reference to the HTTP client is released
/// promptly, reducing the window before the Curl thread is quiesced.
void shutdown();

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

/// Captures a change in a product state, for example when remote
/// configuration enables or disables it.
///
/// The change is sent as an `app-product-change` event with the next
/// heartbeat. If a product changes several times before that, only its latest
/// state is sent.
///
/// @param product The new state of the product. Its `configurations` field is
/// not sent.
void capture_product_change(const Product& product);

/// The `log` namespace provides functions for reporting logs.
namespace log {
/// Report internal warning message to Datadog.
///
/// @param message The warning message to log.
void warning(std::string message);

/// Report internal error message to Datadog.
///
/// @param message The error message.
void error(std::string message);

/// Report internal error message to Datadog.
///
/// @param message The error message.
/// @param stacktrace Stacktrace leading to the error.
void error(std::string message, std::string stacktrace);
}  // namespace log

namespace details {

/// The registry of the running telemetry, or null if telemetry is not running.
MetricRegistry* metric_registry();

/// Calls `create` with the registry of the running telemetry. If telemetry is
/// not running, uses a registry that is never sent instead.
template <typename Create>
auto create_handle(const Create& create) {
  if (MetricRegistry* registry = metric_registry()) return create(*registry);
  MetricRegistry never_sent;
  return create(never_sent);
}

}  // namespace details

/// The `counter` namespace provides functions to track values.
/// Counters can be useful for tracking the total number of an event occurring
/// in one time interval. For example, the amount of requests, errors or jobs
/// processed every 10 seconds.
namespace counter {

/// Creates a counter handle, sent by telemetry. Call it after `init`. Without
/// telemetry, the handle records values that are never sent.
///
/// @param `metric` the counter.
/// @param `fixed_tags` tags sent with every point, as "name:value".
template <typename Tags = details::NoTags>
std::shared_ptr<CounterHandle<Tags>> create(
    Counter metric, std::vector<std::string> fixed_tags = {}) {
  return details::create_handle([&](details::MetricRegistry& registry) {
    return registry.counter<Tags>(std::move(metric), std::move(fixed_tags));
  });
}

}  // namespace counter

/// The `rate` namespace provides support for rate metrics-values.
/// Rates can be useful for tracking the total number of an event occurrences in
/// one time interval. For example, the number of requests per second.
namespace rate {

/// Creates a rate handle, sent by telemetry. Call it after `init`. Without
/// telemetry, the handle records values that are never sent.
///
/// @param `metric` the rate.
/// @param `fixed_tags` tags sent with every point, as "name:value".
template <typename Tags = details::NoTags>
std::shared_ptr<RateHandle<Tags>> create(
    Rate metric, std::vector<std::string> fixed_tags = {}) {
  return details::create_handle([&](details::MetricRegistry& registry) {
    return registry.rate<Tags>(std::move(metric), std::move(fixed_tags));
  });
}

}  // namespace rate

/// The `distribution` namespace provides support for statistical distribution.
/// Distribution can be useful for tracking things like response times or
/// payload sizes.
namespace distribution {

/// Creates a distribution handle, sent by telemetry as DDSketches. Call it
/// after `init`. Without telemetry, the handle records values that are never
/// sent.
///
/// @param `metric` the distribution.
/// @param `fixed_tags` tags sent with every series, as "name:value".
template <typename Tags = details::NoTags>
std::shared_ptr<DistributionHandle<Tags>> create(
    Distribution metric, std::vector<std::string> fixed_tags = {}) {
  return details::create_handle([&](details::MetricRegistry& registry) {
    return registry.distribution<Tags>(std::move(metric),
                                       std::move(fixed_tags));
  });
}

}  // namespace distribution

}  // namespace datadog::telemetry
