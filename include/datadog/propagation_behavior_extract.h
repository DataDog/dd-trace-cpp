#pragma once

// This component provides an `enum class`, `PropagationBehaviorExtract`, that
// indicates how a tracer should handle an incoming trace context on
// extraction: propagate it normally, restart the trace while linking to the
// extracted context, or discard it entirely. See `tracer_config.h`.

#include "optional.h"
#include "string_view.h"

// Undefine legacy Windows macro so it can be a value in the enum
#ifdef IGNORE
#undef IGNORE
#endif

namespace datadog {
namespace tracing {

enum class PropagationBehaviorExtract {
  // Propagate extracted context normally
  CONTINUE,
  // Restart a new trace (new sampling decision, no parent)
  // Reference previous trace through a span-link
  RESTART,
  // Discard entirely incoming context
  IGNORE,
};

StringView to_string_view(PropagationBehaviorExtract behavior);

// defaults to CONTINUE if empty or unsupported
Optional<PropagationBehaviorExtract> parse_propagation_behavior_extract(
    StringView text);

}  // namespace tracing
}  // namespace datadog
