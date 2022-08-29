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

namespace datadog {
namespace tracing {

class Collector;
class Logger;
class SpanSampler;
class TraceSampler;

struct TracerConfig {
  SpanDefaults defaults;

  // `agent` is ignored if `collector` is set.
  DatadogAgentConfig agent;
  std::shared_ptr<Collector> collector = nullptr;

  TraceSamplerConfig trace_sampler;
  SpanSamplerConfig span_sampler;

  PropagationStyles injection_styles;
  PropagationStyles extraction_styles;

  bool report_hostname = false;
  std::shared_ptr<Logger> logger = nullptr;
};

class FinalizedTracerConfig {
  friend Expected<FinalizedTracerConfig> finalize_config(
      const TracerConfig& config);
  FinalizedTracerConfig() = default;

 public:
  SpanDefaults defaults;

  std::shared_ptr<Collector> collector;

  FinalizedTraceSamplerConfig trace_sampler;
  FinalizedSpanSamplerConfig span_sampler;

  PropagationStyles injection_styles;
  PropagationStyles extraction_styles;

  bool report_hostname;
  std::shared_ptr<Logger> logger;
};

Expected<FinalizedTracerConfig> finalize_config(const TracerConfig& config);

}  // namespace tracing
}  // namespace datadog
