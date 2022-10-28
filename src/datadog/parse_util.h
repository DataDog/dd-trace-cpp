#pragma once

// This component provides parsing-related miscellanea.

#include <cstddef>
#include <cstdint>
#include <string_view>

#include "expected.h"

namespace datadog {
namespace tracing {

// Return a `string_view` over the specified range of characters `[begin, end)`.
inline std::string_view range(const char* begin, const char* end) {
  return std::string_view{begin, std::size_t(end - begin)};
}

// Remove leading and trailing whitespace (as determined by `std::isspace`) from
// the specified `input`.
std::string_view strip(std::string_view input);

// Return a non-negative integer parsed from the specified `input` with respect
// to the specified `base`, or return an `Error` if no such integer can be
// parsed. It is an error unless all of `input` is consumed by the parse.
// Leading and trailing whitespace are not ignored.
Expected<std::uint64_t> parse_uint64(std::string_view input, int base);
Expected<int> parse_int(std::string_view input, int base);

// Return a floating point number parsed from the specified `input`, or return
// an `Error` if not such number can be parsed. It is an error unless all of
// `input` is consumed by the parse. Leading and trailing whitespace are not
// ignored.
Expected<double> parse_double(std::string_view input);

// Return whether the specified `prefix` is a prefix of the specified `subject`.
bool starts_with(std::string_view subject, std::string_view prefix);

}  // namespace tracing
}  // namespace datadog
