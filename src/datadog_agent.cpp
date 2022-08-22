#include "datadog_agent.h"

#include "datadog_agent_config.h"

namespace datadog {
namespace tracing {

DatadogAgent::DatadogAgent(const Validated<DatadogAgentConfig>& config) {
  (void)config;
  // TODO
}

std::optional<Error> DatadogAgent::send(
    std::vector<std::unique_ptr<SpanData>>&&,
    const std::function<void(const CollectorResponse&)>&) {
  // TODO
  return std::nullopt;
}

}  // namespace tracing
}  // namespace datadog
