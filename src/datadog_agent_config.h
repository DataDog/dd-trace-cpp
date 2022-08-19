#pragma once

#include <memory>
#include <string>
#include <variant>

#include "validated.h"

namespace datadog {
namespace tracing {

class EventScheduler;
class HTTPClient;

struct DatadogAgentConfig {
  std::shared_ptr<HTTPClient> http_client;
  std::shared_ptr<EventScheduler> event_scheduler;
  std::string agent_url;
  int flush_interval_milliseconds;
};

std::variant<Validated<DatadogAgentConfig>, Error> validate_config(
    const DatadogAgentConfig& config);

}  // namespace tracing
}  // namespace datadog
