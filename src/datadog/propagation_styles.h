#pragma once

// This component provides a `struct`, `PropagationStyles`, that specifies which
// trace context extraction or injection formats are to be used. `TracerConfig`
// has one `PropagationStyles` for extraction and another for injection. See
// `tracer_config.h`.

#include "json_fwd.hpp"

namespace datadog {
namespace tracing {

struct PropagationStyles {
  // Datadog headers, e.g. X-Datadog-Trace-ID
  bool datadog = true;
  // B3 multi-header style, e.g. X-B3-TraceID
  bool b3 = false;
  // The absence of propagation.  If this is the only style set, then
  // propagation is disabled in the relevant direction (extraction or
  // injection).
  bool none = false;
};

nlohmann::json to_json(const PropagationStyles&);

}  // namespace tracing
}  // namespace datadog
