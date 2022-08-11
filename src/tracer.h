#pragma once

#include "error.h"
#include "span.h"
#include "tracer_config.h"

namespace datadog {
namespace tracing {

class DictReader;
class SpanConfig;

class Tracer {
 public:
  explicit Tracer(const TracerConfig& config);

  std::variant<Span, Error> create_span(const SpanConfig& config);
  std::variant<Span, Error> create_span(const Span& parent,
                                        const SpanConfig& config);
  // ...

  std::variant<Span, Error> extract_span(const DictReader& reader);
  std::variant<Span, Error> extract_span(const DictReader& reader,
                                         const SpanConfig& config);
};

}  // namespace tracing
}  // namespace datadog
