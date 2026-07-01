#pragma once

// This component provides an `enum class`, `PropagationBehaviorExtract`, that
// indicates a trace context extraction or injection format to be used.
// `TracerConfig` has one `std::vector<PropagationStyle>` for extraction and
// another for injection. See `tracer_config.h`.

#include "optional.h"
#include "string_view.h"

namespace datadog {
namespace tracing {

enum class PropagationBehaviorExtract {
  // Propagate extract4ed context normally
  CONTINUE,
  // Restart a new trace (new sampling decision, no parent)
  // Reference previous trace through a span-link
  RESTART,
  // Discard entirely incoming context
  IGNORE,
};

StringView to_string_view(PropagationBehaviorExtract behavior);

// defaults to CONTINUE if empty or unsupported
Optional<PropagationBehaviorExtract> parse_propagation_behavior_extract(StringView text);

}  // namespace tracing
}  // namespace datadog
