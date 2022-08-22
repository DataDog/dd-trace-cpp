#include "tracer_config.h"

namespace datadog {
namespace tracing {

std::variant<Validated<TracerConfig>, Error> validate_config(
    const TracerConfig& config) {
  // TODO: environment variables, validation, and other fun.
  TracerConfig after_env{config};

  if (after_env.defaults.service.empty()) {
    return Error{Error::SERVICE_NAME_REQUIRED, "Service name is required."};
  }

  if (const auto* collector =
          std::get_if<std::shared_ptr<Collector>>(&config.collector)) {
    if (!*collector) {
      return Error{Error::NULL_COLLECTOR, "Collector must not be null."};
    }
  } else {
    const auto& agent_config = std::get<DatadogAgentConfig>(config.collector);
    const auto result = validate_config(agent_config);
    if (const auto* error = std::get_if<Error>(&result)) {
      return *error;
    }
    after_env.collector = std::get<Validated<DatadogAgentConfig>>(result);
  }

  // TODO trace_sampler, span_sampler, styles

  return Validated<TracerConfig>(std::move(after_env));
}

}  // namespace tracing
}  // namespace datadog
