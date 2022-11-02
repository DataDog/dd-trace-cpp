#pragma once

// This component provides a `struct`, `PropagationStyles`, that specifies which
// trace context extraction or injection formats are to be used. `TracerConfig`
// has one `PropagationStyles` for extraction and another for injection. See
// `tracer_config.h`.

#include "json_fwd.hpp"

namespace datadog {
namespace tracing {

struct PropagationStyles {
  bool datadog = true;
  bool b3 = false;
};

nlohmann::json to_json(const PropagationStyles&);

}  // namespace tracing
}  // namespace datadog
