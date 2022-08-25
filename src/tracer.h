#pragma once

#include <optional>

#include "clock.h"
#include "error.h"
#include "expected.h"
#include "id_generator.h"
#include "span.h"
#include "tracer_config.h"
#include "validated.h"

namespace datadog {
namespace tracing {

class DictReader;
class SpanConfig;
class TraceSampler;
class SpanSampler;

class Tracer {
  std::shared_ptr<Collector> collector_;
  std::shared_ptr<TraceSampler> trace_sampler_;
  std::shared_ptr<SpanSampler> span_sampler_;
  IDGenerator generator_;
  Clock clock_;
  std::shared_ptr<const SpanDefaults> defaults_;
  PropagationStyles injection_styles_;
  PropagationStyles extraction_styles_;
  std::optional<std::string> hostname_;

 public:
  explicit Tracer(const Validated<TracerConfig>& config);
  Tracer(const Validated<TracerConfig>& config, const IDGenerator& generator,
         const Clock& clock);

  // Create a new trace and return the root span of the trace.  Optionally
  // specify a `config` indicating the attributes of the root span.
  Span create_span(const SpanConfig& config);

  // Return a span whose parent and other context is parsed from the specified
  // `reader`, and whose attributes are determined by the optionally specified
  // `config`.  If there is no tracing information in `reader`, then return an
  // error with code `Error::NO_SPAN_TO_EXTRACT`.  If a failure occurs, then
  // return an error with some other code.
  Expected<Span> extract_span(const DictReader& reader);
  Expected<Span> extract_span(const DictReader& reader,
                              const SpanConfig& config);

  // Return a span extracted from the specified `reader` (see `extract_span`).
  // If there is no span to extract, then return a span that is the root of a
  // new trace (see `create_span`).  Optionally specify a `config` indicating
  // the attributes of the span.  If a failure occurs, then return an error.
  // Note that the absence of a span to extract is not considered an error.
  std::variant<Span, Error> extract_or_create_span(const DictReader& reader);
  std::variant<Span, Error> extract_or_create_span(const DictReader& reader,
                                                   const SpanConfig& config);
};

}  // namespace tracing
}  // namespace datadog
