#include "tracer.h"

#include "datadog_agent.h"
#include "span.h"
#include "span_config.h"
#include "span_data.h"
#include "span_sampler.h"
#include "trace_sampler.h"
#include "trace_segment.h"

namespace datadog {
namespace tracing {

Tracer::Tracer(const Validated<TracerConfig>& config)
    : Tracer(config, default_id_generator, default_clock) {}

Tracer::Tracer(const Validated<TracerConfig>& config,
               const IDGenerator& generator, const Clock& clock)
    : collector_(/* see constructor body */),
      trace_sampler_(std::make_shared<TraceSampler>(
          bless(&TracerConfig::trace_sampler, config))),
      span_sampler_(std::make_shared<SpanSampler>(
          bless(&TracerConfig::span_sampler, config))),
      generator_(generator),
      clock_(clock),
      defaults_(config.defaults) {
  if (auto* collector =
          std::get_if<std::shared_ptr<Collector>>(&config.collector)) {
    collector_ = *collector;
  } else {
    collector_ =
        std::make_shared<DatadogAgent>(bless(&TracerConfig::collector, config));
  }
}

Span Tracer::create_span(const SpanConfig& config) {
  auto span_data = std::make_unique<SpanData>();
  span_data->service = config.service.value_or(defaults_.service);
  span_data->name = config.name.value_or(defaults_.name);
  span_data->resource = config.resource.value_or(span_data->name);
  span_data->service_type =
      config.service_type.value_or(defaults_.service_type);
  if (config.start) {
    span_data->start = *config.start;
  } else {
    span_data->start = clock_();
  }
  span_data->tags = defaults_.tags;
  span_data->tags.insert(config.tags.begin(), config.tags.end());
  span_data->trace_id = generator_.generate_trace_id();
  span_data->span_id = generator_.generate_span_id();
  span_data->parent_id = 0;

  const auto span_data_ptr = span_data.get();
  const auto segment = std::make_shared<TraceSegment>(
      collector_, trace_sampler_, span_sampler_,
      std::nullopt /* sampling_decision */, std::move(span_data));
  Span span{span_data_ptr, segment, generator_.generate_span_id, clock_};
  return span;
}

}  // namespace tracing
}  // namespace datadog
