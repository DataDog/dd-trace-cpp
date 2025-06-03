#pragma once

// This component provides a `struct InjectionOptions` containing optional
// parameters to `Span::inject` that alter the behavior of trace context
// propagation.

namespace datadog {
namespace tracing {

struct InjectionOptions {
  bool has_appsec_matches{};
};

}  // namespace tracing
}  // namespace datadog
