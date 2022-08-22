#include "datadog_agent_config.h"

#include "threaded_event_scheduler.h"

namespace datadog {
namespace tracing {

std::variant<Validated<DatadogAgentConfig>, Error> validate_config(
    const DatadogAgentConfig& config) {
  DatadogAgentConfig after_env{config};

  if (!config.http_client) {
    return Error{Error::DATADOG_AGENT_NULL_HTTP_CLIENT,
                 "DatadogAgent: HTTP client cannot be null."};
  }
  if (!config.event_scheduler) {
    after_env.event_scheduler = std::make_shared<ThreadedEventScheduler>();
  }
  if (config.flush_interval_milliseconds <= 0) {
    return Error{Error::DATADOG_AGENT_INVALID_FLUSH_INTERVAL,
                 "DatadogAgent: Flush interval must be a positive number of "
                 "milliseconds."};
  }
  // TODO: parse agent_url
  return Validated<DatadogAgentConfig>{config};
}

}  // namespace tracing
}  // namespace datadog
