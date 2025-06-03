#pragma once

// This component provides a `struct InjectionOptions` containing optional
// parameters to `Span::inject` that alter the behavior of trace context
// propagation.

#include <array>

#include "optional.h"

namespace datadog {
namespace tracing {

struct InjectionOptions {
  // If DD_APM_TRACING_ENABLED=false and what we're injecting is not an APM
  // trace, then the code for the trace source (e.g. 02 for Appsec) can be
  // set here.
  Optional<std::array<char, 2>> trace_source{};
};

}  // namespace tracing
}  // namespace datadog
