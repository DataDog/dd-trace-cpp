#pragma once

#include <memory>
#include <optional>
#include <string>
#include <variant>

#include "datadog_agent_config.h"
#include "error.h"
#include "propagation_styles.h"
#include "span_sampler_config.h"
#include "trace_sampler_config.h"
#include "validated.h"

namespace datadog {
namespace tracing {

class Collector;

struct TracerConfig {
  struct {
    std::string service;
    std::optional<std::string> service_type;
    std::optional<std::string> environment;
    std::optional<std::string> version;
    std::optional<std::string> operation;  // a.k.a. name
    // ...
  } defaults;  // TODO: Do spans inherit values from their parents, from
               // this, or from both?

  std::variant<DatadogAgentConfig, std::shared_ptr<Collector>> collector;
  TraceSamplerConfig trace_sampler;
  SpanSamplerConfig span_sampler;

  PropagationStyles injection_styles;
  PropagationStyles extraction_styles;
};

std::variant<Validated<TracerConfig>, Error> validate_config(
    const TracerConfig& config);

}  // namespace tracing
}  // namespace datadog
