#pragma once

// This component provides a struct, `TracerConfig`, used to configure a
// `Tracer`.  `Tracer` is instantiated with a `FinalizedTracerConfig`, which
// must be obtained from the result of a call to `finalize_config`.

#include <cstddef>
#include <memory>
#include <variant>
#include <vector>

#include "datadog_agent_config.h"
#include "error.h"
#include "expected.h"
#include "propagation_style.h"
#include "runtime_id.h"
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
  // `defaults` are properties that spans created by the tracer will have unless
  // overridden at the time of their creation.  See `span_defaults.h`.  Note
  // that `defaults.service` is required to have a nonempty value.
  SpanDefaults defaults;

  // `agent` configures a `DatadogAgent` collector instance.  See
  // `datadog_agent_config.h`.  Note that `agent` is ignored if `collector` is
  // set or if `report_traces` is `false`.
  DatadogAgentConfig agent;

  // `collector` is a `Collector` instance that the tracer will use to report
  // traces to Datadog.  If `collector` is null, then a `DatadogAgent` instance
  // will be created using the `agent` configuration.  Note that `collector` is
  // ignored if `report_traces` is `false`.
  std::shared_ptr<Collector> collector = nullptr;

  // `report_traces` indicates whether traces generated by the tracer will be
  // sent to a collector (`true`) or discarded on completion (`false`).  If
  // `report_traces` is `false`, then both `agent` and `collector` are ignored.
  // `report_traces` is overridden by the `DD_TRACE_ENABLED` environment
  // variable.
  bool report_traces = true;

  // `report_telemetry` indicates whether telemetry about the tracer will be
  // sent to a collector (`true`) or discarded on completion (`false`).  If
  // `report_telemetry` is `false`, then this feature is disabled.
  // `report_telemetry` is overridden by the
  // `DD_INSTRUMENTATION_TELEMETRY_ENABLED` environment variable.
  bool report_telemetry = true;

  // `trace_sampler` configures trace sampling.  Trace sampling determines which
  // traces are sent to Datadog.  See `trace_sampler_config.h`.
  TraceSamplerConfig trace_sampler;

  // `span_sampler` configures span sampling.  Span sampling allows specified
  // spans to be sent to Datadog even when their enclosing trace is dropped by
  // the trace sampler.  See `span_sampler_config.h`.
  SpanSamplerConfig span_sampler;

  // `injection_styles` indicates with which tracing systems trace propagation
  // will be compatible when injecting (sending) trace context.
  // All styles indicated by `injection_styles` are used for injection.
  // `injection_styles` is overridden by the `DD_TRACE_PROPAGATION_STYLE_INJECT`
  // and `DD_TRACE_PROPAGATION_STYLE` environment variables.
  std::vector<PropagationStyle> injection_styles = {PropagationStyle::DATADOG};

  // `extraction_styles` indicates with which tracing systems trace propagation
  // will be compatible when extracting (receiving) trace context.
  // Extraction styles are applied in the order in which they appear in
  // `extraction_styles`. The first style that produces trace context or
  // produces an error determines the result of extraction.
  // `extraction_styles` is overridden by the
  // `DD_TRACE_PROPAGATION_STYLE_EXTRACT` and `DD_TRACE_PROPAGATION_STYLE`
  // environment variables.
  std::vector<PropagationStyle> extraction_styles = {PropagationStyle::DATADOG};

  // `report_hostname` indicates whether the tracer will include the result of
  // `gethostname` with traces sent to the collector.
  bool report_hostname = false;

  // `tags_header_size` is the maximum allowed size, in bytes, of the serialized
  // value of the "X-Datadog-Tags" header used when injecting trace context for
  // propagation.  If the serialized value of the header would exceed
  // `tags_header_size`, the header will be omitted instead.
  std::size_t tags_header_size = 512;

  // `logger` specifies how the tracer will issue diagnostic messages.  If
  // `logger` is null, then it defaults to a logger that inserts into
  // `std::cerr`.
  std::shared_ptr<Logger> logger = nullptr;

  // `log_on_startup` indicates whether the tracer will log a banner of
  // configuration information once initialized.
  // `log_on_startup` is overridden by the `DD_TRACE_STARTUP_LOGS` environment
  // variable.
  bool log_on_startup = true;

  // `trace_id_128_bit` indicates whether the tracer will generate 128-bit trace
  // IDs.  If true, the tracer will generate 128-bit trace IDs. If false, the
  // tracer will generate 64-bit trace IDs. `trace_id_128_bit` is overridden by
  // the `DD_TRACE_128_BIT_TRACEID_GENERATION_ENABLED` environment variable.
  bool trace_id_128_bit = true;

  // `runtime_id` denotes the current run of the application in which the tracer
  // is embedded. If `runtime_id` is not specified, then it defaults to a
  // pseudo-randomly generated value. A server that contains multiple tracers,
  // such as those in the worker threads/processes of a reverse proxy, might
  // specify the same `runtime_id` for all tracer instances in the same run.
  Optional<RuntimeID> runtime_id;
};

// `FinalizedTracerConfig` contains `Tracer` implementation details derived from
// a valid `TracerConfig` and accompanying environment.
// `FinalizedTracerConfig` must be obtained by calling `finalize_config`.
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

  std::vector<PropagationStyle> injection_styles;
  std::vector<PropagationStyle> extraction_styles;

  bool report_hostname;
  std::size_t tags_header_size;
  std::shared_ptr<Logger> logger;
  bool log_on_startup;
  bool trace_id_128_bit;
  bool report_telemetry;
  Optional<RuntimeID> runtime_id;
};

// Return a `FinalizedTracerConfig` from the specified `config` and from any
// relevant environment variables.  If any configuration is invalid, return an
// `Error`.
Expected<FinalizedTracerConfig> finalize_config(const TracerConfig& config);

}  // namespace tracing
}  // namespace datadog
