#include "datadog_agent.h"

#include "collector_response.h"
#include "datadog_agent_config.h"
#include "span_data.h"

namespace datadog {
namespace tracing {

DatadogAgent::DatadogAgent(const Validated<DatadogAgentConfig>& config) {
  (void)config;
  // TODO
}

std::optional<Error> DatadogAgent::send(
    std::vector<std::unique_ptr<SpanData>>&& spans,
    const std::function<void(CollectorResponse)>& on_response) {
  spans.clear();                     // TODO
  on_response(CollectorResponse{});  // TODO
  return std::nullopt;
}

}  // namespace tracing
}  // namespace datadog
