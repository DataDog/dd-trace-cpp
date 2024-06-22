#pragma once

#include "config.h"
#include "datadog/datadog_agent_config.h"
#include "datadog/expected.h"

namespace datadog::tracing {

class FinalizedDatadogAgentConfig {
  friend Expected<FinalizedDatadogAgentConfig> finalize_config(
      const DatadogAgentConfig&, const std::shared_ptr<Logger>&, const Clock&);

  FinalizedDatadogAgentConfig() = default;

 public:
  Clock clock;
  bool remote_configuration_enabled;
  std::shared_ptr<HTTPClient> http_client;
  std::shared_ptr<EventScheduler> event_scheduler;
  HTTPClient::URL url;
  std::chrono::steady_clock::duration flush_interval;
  std::chrono::steady_clock::duration request_timeout;
  std::chrono::steady_clock::duration shutdown_timeout;
  std::chrono::steady_clock::duration remote_configuration_poll_interval;
  std::unordered_map<ConfigName, ConfigMetadata> metadata;
};
}  // namespace datadog::tracing
