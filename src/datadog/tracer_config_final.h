#pragma once

#include <variant>

#include "datadog/expected.h"
#include "datadog/tracer_config.h"
#include "datadog_config_final.h"
#include "span_defaults.h"
#include "span_sampler_config_final.h"
#include "trace_sampler_config_final.h"

namespace datadog::tracing {

// `FinalizedTracerConfig` contains `Tracer` implementation details derived from
// a valid `TracerConfig` and accompanying environment.
// `FinalizedTracerConfig` must be obtained by calling `finalize_config`.
class FinalizedTracerConfig final {
  friend Expected<FinalizedTracerConfig> finalize_config(
      const TracerConfig &config, const Clock &clock);
  FinalizedTracerConfig() = default;

 public:
  SpanDefaults defaults;

  std::variant<std::monostate, FinalizedDatadogAgentConfig,
               std::shared_ptr<Collector>>
      collector;

  FinalizedTraceSamplerConfig trace_sampler;
  FinalizedSpanSamplerConfig span_sampler;

  std::vector<PropagationStyle> injection_styles;
  std::vector<PropagationStyle> extraction_styles;

  bool report_hostname;
  std::size_t tags_header_size;
  std::shared_ptr<Logger> logger;
  bool log_on_startup;
  bool generate_128bit_trace_ids;
  bool report_telemetry;
  Optional<RuntimeID> runtime_id;
  Clock clock;
  std::string integration_name;
  std::string integration_version;
  bool delegate_trace_sampling;
  bool report_traces;
  std::unordered_map<ConfigName, ConfigMetadata> metadata;
};
}  // namespace datadog::tracing
