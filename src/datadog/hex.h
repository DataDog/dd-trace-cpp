#pragma once

// This component provides a function, `hex`, for formatting an integral value
// in hexadecimal.

#include <cassert>
#include <charconv>
#include <limits>
#include <system_error>
#include <utility>

namespace datadog {
namespace tracing {

// Return the specified `value` formatted as a lower-case hexadecimal string
// without any leading zeroes.
template <typename Integer>
std::string hex(Integer value) {
  // 4 bits per hex digit char, and then +1 char for possible minus sign
  char buffer[std::numeric_limits<Integer>::digits / 4 + 1];

  const int base = 16;
  auto result =
      std::to_chars(std::begin(buffer), std::end(buffer), value, base);
  assert(result.ec == std::errc());

  return std::string{std::begin(buffer), result.ptr};
}

}  // namespace tracing
}  // namespace datadog