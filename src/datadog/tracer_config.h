#pragma once

#include <cstddef>
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
  // `collector` and `agent` are ignored if `report_traces` is `false`.
  bool report_traces = true;

  TraceSamplerConfig trace_sampler;
  SpanSamplerConfig span_sampler;

  PropagationStyles injection_styles;
  PropagationStyles extraction_styles;

  bool report_hostname = false;
  std::size_t tags_header_size = 512;
  std::shared_ptr<Logger> logger = nullptr;
  bool log_on_startup = true;
};

class FinalizedTracerConfig {
  friend Expected<FinalizedTracerConfig> finalize_config(
      const TracerConfig& config);
  FinalizedTracerConfig() = default;

 public:
  SpanDefaults defaults;

  std::variant<std::monostate, FinalizedDatadogAgentConfig,
               std::shared_ptr<Collector>>
      collector;

  FinalizedTraceSamplerConfig trace_sampler;
  FinalizedSpanSamplerConfig span_sampler;

  PropagationStyles injection_styles;
  PropagationStyles extraction_styles;

  bool report_hostname;
  std::size_t tags_header_size;
  std::shared_ptr<Logger> logger;
  bool log_on_startup;
};

Expected<FinalizedTracerConfig> finalize_config(const TracerConfig& config);

}  // namespace tracing
}  // namespace datadog
