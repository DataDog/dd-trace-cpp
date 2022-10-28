#pragma once

// This component provides a `struct`, `PropagationStyles`, that specifies which
// trace context extraction or injection formats are to be used. `TracerConfig`
// has one `PropagationStyles` for extraction and another for injection. See
// `tracer_config.h`.

namespace datadog {
namespace tracing {

struct PropagationStyles {
  bool datadog = true;
  bool b3 = false;
};

}  // namespace tracing
}  // namespace datadog
