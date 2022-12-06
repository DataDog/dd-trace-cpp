#pragma once

// TODO: explain

#ifdef DD_USE_ABSEIL_FOR_ENVOY
// Abseil examples, including usage in Envoy, include Abseil headers in quoted
// style instead of angle bracket style, per Bazel's default build behavior.
#include "absl/types/optional.h"
#else
#include <optional>
#endif  // defined DD_USE_ABSEIL_FOR_ENVOY

namespace datadog {
namespace tracing {

#ifdef DD_USE_ABSEIL_FOR_ENVOY
template <typename Value>
using Optional = absl::optional<Value>;
#else
template <typename Value>
using Optional = std::optional<Value>;
#endif  // defined DD_USE_ABSEIL_FOR_ENVOY

}  // namespace tracing
}  // namespace datadog
