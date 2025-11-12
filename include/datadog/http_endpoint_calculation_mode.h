#pragma once

// This component provides an enumeration that controls how the http.endpoint
// tag is calculated for HTTP spans.

#include <cstdint>

namespace datadog {
namespace tracing {

// `HttpEndpointCalculationMode` determines when and how the http.endpoint tag
// is inferred from http.url for HTTP spans.
//
// The http.endpoint tag provides a normalized, parameterized version of the
// HTTP path (e.g., "/users/{param:int}" instead of "/users/123"). This helps
// aggregate similar requests and reduce cardinality in monitoring systems.
enum class HttpEndpointCalculationMode : std::uint8_t {
  // Do not calculate http.endpoint. The tag will not be set unless explicitly
  // provided by the user.
  DISABLED,

  // Calculate http.endpoint from http.url only when http.route is not present.
  // This mode acts as a fallback - if instrumentation provides http.route,
  // use that; otherwise, infer http.endpoint from the URL path.
  FALLBACK,

  // Always calculate http.endpoint from http.url, even when http.route is
  // present. Both tags will be set, allowing for comparison between
  // user-provided routes and automatically inferred endpoints.
  ALWAYS_CALCULATE,
};

}  // namespace tracing
}  // namespace datadog
