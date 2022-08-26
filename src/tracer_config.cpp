#include "tracer_config.h"

namespace datadog {
namespace tracing {

Expected<Validated<TracerConfig>> validate_config(const TracerConfig& config) {
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
    auto validated = validate_config(agent_config);
    if (auto* error = validated.if_error()) {
      return std::move(*error);
    }
    after_env.collector = std::move(*validated);
  }

  // TODO trace_sampler, span_sampler

  /*struct PropagationStyles {
    bool datadog = true;
    bool b3 = false;
    bool w3c = false;
  };*/
  const auto not_implemented = [](std::string_view style,
                                  std::string_view operation) {
    std::string message;
    message += "The ";
    message += style;
    message += ' ';
    message += operation;
    message += " style is not yet supported. Only datadog is supported.";
    return Error{Error::NOT_IMPLEMENTED, std::move(message)};
  };

  if (config.extraction_styles.b3) {
    return not_implemented("b3", "extraction");
  } else if (config.extraction_styles.w3c) {
    return not_implemented("w3c", "extraction");
  } else if (config.injection_styles.b3) {
    return not_implemented("b3", "injection");
  } else if (config.injection_styles.w3c) {
    return not_implemented("w3c", "injection");
  } else if (!config.extraction_styles.datadog) {
    return Error{Error::MISSING_SPAN_EXTRACTION_STYLE,
                 "At least one extraction style must be specified."};
  } else if (!config.injection_styles.datadog) {
    return Error{Error::MISSING_SPAN_INJECTION_STYLE,
                 "At least one injection style must be specified."};
  }

  return Validated<TracerConfig>(std::move(after_env));
}

}  // namespace tracing
}  // namespace datadog
