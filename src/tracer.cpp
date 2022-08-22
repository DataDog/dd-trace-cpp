#include "tracer.h"

#include "datadog_agent.h"
#include "span_sampler.h"
#include "trace_sampler.h"

namespace datadog {
namespace tracing {

/*class Tracer {
  std::shared_ptr<Collector> collector_;
  std::shared_ptr<TraceSampler> trace_sampler_;
  std::shared_ptr<SpanSampler> span_sampler_;
  IDGenerator generator;
  Clock clock;
  explicit Tracer(const TracerConfig& config);
  Tracer(const TracerConfig& config, const IDGenerator& generator,
         const Clock& clock);
  Span create_span(const SpanConfig& config);
  std::variant<Span, Error> extract_span(const DictReader& reader);
  std::variant<Span, Error> extract_span(const DictReader& reader,
                                         const SpanConfig& config);
};*/

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
      clock_(clock) {
  if (auto* collector =
          std::get_if<std::shared_ptr<Collector>>(&config.collector)) {
    collector_ = *collector;
  } else {
    collector_ =
        std::make_shared<DatadogAgent>(bless(&TracerConfig::collector, config));
  }
}

}  // namespace tracing
}  // namespace datadog
