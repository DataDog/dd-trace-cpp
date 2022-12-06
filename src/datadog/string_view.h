#pragma once

// TODO: explain

#ifdef DD_USE_ABSEIL_FOR_ENVOY
// Abseil examples, including usage in Envoy, include Abseil headers in quoted
// style instead of angle bracket style, per Bazel's default build behavior.
#include "absl/strings/string_view.h"
#else
#include <string_view>
#endif  // defined DD_USE_ABSEIL_FOR_ENVOY

namespace datadog {
namespace tracing {

#ifdef DD_USE_ABSEIL_FOR_ENVOY
using StringView = absl::string_view;
#else
using StringView = std::string_view;
#endif  // defined DD_USE_ABSEIL_FOR_ENVOY

}  // namespace tracing
}  // namespace datadog
