#pragma once

#include <memory>
#include <variant>

#include "datadog_agent_config.h"
#include "error.h"
#include "expected.h"
#include "propagation_styles.h"
#include "span_defaults.h"
#include "span_sampler_config.h"
#include "trace_sampler_config.h"
#include "validated.h"

namespace datadog {
namespace tracing {

class Collector;

struct TracerConfig {
  SpanDefaults defaults;

  std::variant<DatadogAgentConfig, std::shared_ptr<Collector>> collector;
  TraceSamplerConfig trace_sampler;
  SpanSamplerConfig span_sampler;

  PropagationStyles injection_styles;
  PropagationStyles extraction_styles;

  bool report_hostname = false;
};

Expected<Validated<TracerConfig>> validate_config(const TracerConfig& config);

}  // namespace tracing
}  // namespace datadog
