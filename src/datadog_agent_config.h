#pragma once

#include <memory>
#include <string>
#include <string_view>
#include <variant>

#include "error.h"
#include "http_client.h"
#include "validated.h"

namespace datadog {
namespace tracing {

class EventScheduler;

struct DatadogAgentConfig {
  std::shared_ptr<HTTPClient> http_client;
  std::shared_ptr<EventScheduler> event_scheduler = nullptr;
  std::string agent_url = "http://localhost:8126";
  int flush_interval_milliseconds = 2000;

  static std::variant<HTTPClient::URL, Error> parse(std::string_view);
};

std::variant<Validated<DatadogAgentConfig>, Error> validate_config(
    const DatadogAgentConfig& config);

}  // namespace tracing
}  // namespace datadog
