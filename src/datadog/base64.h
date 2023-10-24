#pragma once

#include <stdint.h>

#include <iostream>
#include <sstream>

#include "datadog/string_view.h"

namespace datadog {
namespace tracing {
namespace base64 {

inline constexpr StringView base64_chars{
    "ABCDEFGHIJKLMNOPQRSTUVWXYZ"
    "abcdefghijklmnopqrstuvwxyz"
    "0123456789+/"};

// Temporary base64 decoder, copied from github:
// https://github.com/tobiaslocker/base64/blob/master/include/base64.hpp
template <class OutputBuffer>
inline OutputBuffer decode_into(std::string_view data) {
  using value_type = typename OutputBuffer::value_type;
  static_assert(std::is_same_v<value_type, char> ||
                std::is_same_v<value_type, unsigned char> ||
                std::is_same_v<value_type, std::byte>);

  size_t counter = 0;
  uint32_t bit_stream = 0;
  OutputBuffer decoded;
  decoded.reserve(std::size(data));
  for (unsigned char c : data) {
    auto const num_val = base64_chars.find(c);
    if (num_val != std::string::npos) {
      auto const offset = 18 - counter % 4 * 6;
      bit_stream += static_cast<uint32_t>(num_val) << offset;
      if (offset == 12) {
        decoded.push_back(static_cast<value_type>(bit_stream >> 16 & 0xff));
      }
      if (offset == 6) {
        decoded.push_back(static_cast<value_type>(bit_stream >> 8 & 0xff));
      }
      if (offset == 0 && counter != 4) {
        decoded.push_back(static_cast<value_type>(bit_stream & 0xff));
        bit_stream = 0;
      }
    } else if (c != '=') {
      throw std::runtime_error{"Invalid base64 encoded data"};
    }
    counter++;
  }
  return decoded;
}

inline std::string decode(std::string_view data) {
  return decode_into<std::string>(data);
}

std::string decode_v2(std::string_view in);

}  // namespace base64
}  // namespace tracing
}  // namespace datadog
