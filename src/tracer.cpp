#include "tracer.h"

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

Tracer::Tracer(const TracerConfig& config) {
  // TODO
  (void)config;
}

}  // namespace tracing
}  // namespace datadog
