#pragma once

// This component provides a `struct InjectionOptions` containing optional
// parameters to `Span::inject` that alter the behavior of trace context
// propagation.

namespace datadog {
namespace tracing {

struct InjectionOptions {
  // If this tracer is using the "Datadog" propagation injection style, then
  // include a request header that indicates that whoever extracts this trace
  // context "on the other side" may make their own trace sampling decision
  // and convey it back to us in a response header.
  bool delegate_sampling_decision;
};

}  // namespace tracing
}  // namespace datadog
