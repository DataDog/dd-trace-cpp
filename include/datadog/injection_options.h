#pragma once

// This component provides a `struct InjectionOptions` containing optional
// parameters to `Span::inject` that alter the behavior of trace context
// propagation.

namespace datadog {
namespace tracing {

struct InjectionOptions {
  /// Enforce context injection when `DD_APM_TRACING_ENABLED=false`.
  bool force;
};

}  // namespace tracing
}  // namespace datadog
