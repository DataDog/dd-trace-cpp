#pragma once

#include <cstdint>
#include <string_view>

#include "expected.h"

namespace datadog {
namespace tracing {

Expected<std::uint64_t> parse_uint64(std::string_view input, int base);

Expected<int> parse_int(std::string_view input, int base);

Expected<double> parse_double(std::string_view input);

}  // namespace tracing
}  // namespace datadog
