#pragma once

#include <memory>
#include <string>
#include <variant>

#include "error.h"
#include "validated.h"

namespace datadog {
namespace tracing {

class EventScheduler;
class HTTPClient;

struct DatadogAgentConfig {
  std::shared_ptr<HTTPClient> http_client;
  std::shared_ptr<EventScheduler> event_scheduler = nullptr;
  std::string agent_url = "http://localhost:8126";
  int flush_interval_milliseconds = 2000;
};

std::variant<Validated<DatadogAgentConfig>, Error> validate_config(
    const DatadogAgentConfig& config);

}  // namespace tracing
}  // namespace datadog
