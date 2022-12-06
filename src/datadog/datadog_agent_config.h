#pragma once

// This component provides facilities for configuring a `DatadogAgent`.
//
// `struct DatadogAgentConfig` contains fields that are used to configure
// `DatadogAgent`.  The configuration must first be finalized before it can be
// used by `DatadogAgent`.  The function `finalize_config` produces either an
// error or a `FinalizedDatadogAgentConfig`.  The latter can be used by
// `DatadogAgent`.
//
// Typical usage of `DatadogAgentConfig` is implicit as part of `TracerConfig`.
// See `tracer_config.h`.

#include <chrono>
#include <memory>
#include <string>
#include <variant>

#include "expected.h"
#include "http_client.h"
#include "string_view.h"

namespace datadog {
namespace tracing {

class EventScheduler;
class Logger;

struct DatadogAgentConfig {
  // The `HTTPClient` used to submit traces to the Datadog Agent.  If this
  // library was built with libcurl (the default), then `http_client` is
  // optional: a `Curl` instance will be used if `http_client` is left null.
  // If this library was built without libcurl, then `http_client` is required
  // not to be null.
  std::shared_ptr<HTTPClient> http_client;
  // The `EventScheduler` used to periodically submit batches of traces to the
  // Datadog Agent.  If `event_scheduler` is null, then a
  // `ThreadedEventScheduler` instance will be used instead.
  std::shared_ptr<EventScheduler> event_scheduler = nullptr;
  // A URL at which the Datadog Agent can be contacted.
  // The following formats are supported:
  //
  // - http://<domain or IP>:<port>
  // - http://<domain or IP>
  // - http+unix://<path to socket>
  // - unix://<path to socket>
  //
  // The port defaults to 8126 if it is not specified.
  std::string url = "http://localhost:8126";
  // How often, in milliseconds, to send batches of traces to the Datadog Agent.
  int flush_interval_milliseconds = 2000;

  static Expected<HTTPClient::URL> parse(StringView);
};

class FinalizedDatadogAgentConfig {
  friend Expected<FinalizedDatadogAgentConfig> finalize_config(
      const DatadogAgentConfig& config, const std::shared_ptr<Logger>& logger);

  FinalizedDatadogAgentConfig() = default;

 public:
  std::shared_ptr<HTTPClient> http_client;
  std::shared_ptr<EventScheduler> event_scheduler;
  HTTPClient::URL url;
  std::chrono::steady_clock::duration flush_interval;
};

Expected<FinalizedDatadogAgentConfig> finalize_config(
    const DatadogAgentConfig& config, const std::shared_ptr<Logger>& logger);

}  // namespace tracing
}  // namespace datadog
