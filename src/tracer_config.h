#pragma once

#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
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
  struct SpanDefaults {
    std::string service;
    std::string service_type = "web";
    std::string environment = "";
    std::string version = "";
    std::string name = "";
    std::unordered_map<std::string, std::string> tags;
  };

  SpanDefaults defaults;

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
