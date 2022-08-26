#pragma once

#include <chrono>
#include <memory>
#include <string>
#include <string_view>
#include <variant>

#include "expected.h"
#include "http_client.h"

namespace datadog {
namespace tracing {

class EventScheduler;

struct DatadogAgentConfig {
  std::shared_ptr<HTTPClient> http_client;
  std::shared_ptr<EventScheduler> event_scheduler = nullptr;
  std::string url = "http://localhost:8126";
  int flush_interval_milliseconds = 2000;

  static Expected<HTTPClient::URL> parse(std::string_view);
};

class FinalizedDatadogAgentConfig {
  friend Expected<FinalizedDatadogAgentConfig> finalize_config(
      const DatadogAgentConfig& config);

  FinalizedDatadogAgentConfig() = default;

 public:
  std::shared_ptr<HTTPClient> http_client;
  std::shared_ptr<EventScheduler> event_scheduler;
  HTTPClient::URL url;
  std::chrono::steady_clock::duration flush_interval;
};

Expected<FinalizedDatadogAgentConfig> finalize_config(
    const DatadogAgentConfig& config);

}  // namespace tracing
}  // namespace datadog
