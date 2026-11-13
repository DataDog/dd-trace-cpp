#pragma once

#include <datadog/string_view.h>

#include <string>

namespace datadog {
namespace tracing {

// Return the result of decoding the specified padded base64-encoded `input`. If
// `input` is not padded, then return the empty string instead.
std::string base64_decode(StringView input);

// Return the padded base64 encoding of the specified `input`.
std::string base64_encode(StringView input);

}  // namespace tracing
}  // namespace datadog
