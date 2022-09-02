#pragma once

#include <charconv>
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

std::string_view strip(std::string_view input);

Expected<std::uint64_t> parse_uint64(std::string_view input, int base);

Expected<int> parse_int(std::string_view input, int base);

Expected<double> parse_double(std::string_view input, std::chars_format format);

}  // namespace tracing
}  // namespace datadog
