#pragma once

#include "string_view.h"

namespace datadog {
namespace tracing {
namespace base64 {

/*
 * Decode a base64-encoded string and returns the decoded data.
 *
 * It only supported padded base64-encoded string. Providing an unpadded
 * input will return an empty string.
 */
std::string decode(StringView in);

}  // namespace base64
}  // namespace tracing
}  // namespace datadog
