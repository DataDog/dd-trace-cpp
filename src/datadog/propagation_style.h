#pragma once

// This component provides an `enum class`, `PropagationStyle`, that indicates a
// trace context extraction or injection format to be used. `TracerConfig` has
// one `std::vector<PropagationStyle>` for extraction and another for injection.
// See `tracer_config.h`.

#include <vector>

#include "json_fwd.hpp"

namespace datadog {
namespace tracing {

enum class PropagationStyle {
  // Datadog headers, e.g. X-Datadog-Trace-ID
  DATADOG,
  // B3 multi-header style, e.g. X-B3-TraceID
  B3,
  // W3C headers style, i.e. traceparent and tracestate
  W3C,
  // The absence of propagation.  If this is the only style set, then
  // propagation is disabled in the relevant direction (extraction or
  // injection).
  NONE,
};

nlohmann::json to_json(PropagationStyle style);
nlohmann::json to_json(const std::vector<PropagationStyle>& styles);

}  // namespace tracing
}  // namespace datadog
