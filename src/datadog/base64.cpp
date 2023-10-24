#include "base64.h"

namespace datadog {
namespace tracing {
namespace base64 {

#define _ 255
#define SENTINEL_VALUE _

/*
 * Lookup table mapping the base64 table. Invalid inputs are mapped
 * to the value 255.
 */
constexpr uint8_t k_base64_table[]{
    _,  _,  _,  _,  _,  _,  _,  _,  _,  _,  _,  _,  _,  _,  _,  _,  _,  _,  _,
    _,  _,  _,  _,  _,  _,  _,  _,  _,  _,  _,  _,  _,  _,  _,  _,  _,  _,  _,
    _,  _,  _,  _,  _,  62, _,  _,  _,  63, 52, 53, 54, 55, 56, 57, 58, 59, 60,
    61, _,  _,  _,  _,  _,  _,  _,  0,  1,  2,  3,  4,  5,  6,  7,  8,  9,  10,
    11, 12, 13, 14, 15, 16, 17, 18, 19, 20, 21, 22, 23, 24, 25, _,  _,  _,  _,
    _,  _,  26, 27, 28, 29, 30, 31, 32, 33, 34, 35, 36, 37, 38, 39, 40, 41, 42,
    43, 44, 45, 46, 47, 48, 49, 50, 51, _,  _,  _,  _,  _,  _,  _,  _,  _,  _,
    _,  _,  _,  _,  _,  _,  _,  _,  _,  _,  _,  _,  _,  _,  _,  _,  _,  _,  _,
    _,  _,  _,  _,  _,  _,  _,  _,  _,  _,  _,  _,  _,  _,  _,  _,  _,  _,  _,
    _,  _,  _,  _,  _,  _,  _,  _,  _,  _,  _,  _,  _,  _,  _,  _,  _,  _,  _,
    _,  _,  _,  _,  _,  _,  _,  _,  _,  _,  _,  _,  _,  _,  _,  _,  _,  _,  _,
    _,  _,  _,  _,  _,  _,  _,  _,  _,  _,  _,  _,  _,  _,  _,  _,  _,  _,  _,
    _,  _,  _,  _,  _,  _,  _,  _,  _,  _,  _,  _,  _,  _,  _,  _,  _,  _,  _,
    _,  _,  _,  _,  _,  _,  _,  _,  _};

// TODO: support input without padding
std::string decode_v2(std::string_view in) {
  const std::size_t in_size = in.size();

  std::string out;
  out.reserve(in_size);

  union {
    uint32_t buffer;
    uint8_t bytes[4];
  } decoder;

  std::size_t padding;
  if (in[in_size - 2] == '=') {
    padding = 2;
  } else if (in[in_size - 1] == '=') {
    padding = 1;
  } else {
    padding = 0;
  }

  std::size_t i = 0;
  const std::size_t end = (padding == 0) ? in_size : in_size - 4;

  for (; i + 4 <= end;) {
    auto c0 = k_base64_table[static_cast<size_t>(in[i++])];
    auto c1 = k_base64_table[static_cast<size_t>(in[i++])];
    auto c2 = k_base64_table[static_cast<size_t>(in[i++])];
    auto c3 = k_base64_table[static_cast<size_t>(in[i++])];

    if (c0 == SENTINEL_VALUE || c1 == SENTINEL_VALUE || c2 == SENTINEL_VALUE ||
        c3 == SENTINEL_VALUE) {
      return "";
    }

    decoder.buffer =
        static_cast<uint32_t>(0) | static_cast<uint32_t>(c0) << 26 |
        static_cast<uint32_t>(c1) << 20 | static_cast<uint32_t>(c2) << 14 |
        static_cast<uint32_t>(c3) << 8;

    // NOTE(@dmehala): It might seem confusion to read those bytes in reverse
    // order. It is related to the architecture endianess. For now the set of
    // architecture we support (x86_64 and arm64) are all little-endian.
    out.push_back(decoder.bytes[3]);
    out.push_back(decoder.bytes[2]);
    out.push_back(decoder.bytes[1]);
  }

  switch (padding) {
    case 1: {
      auto c0 = k_base64_table[static_cast<size_t>(in[i++])];
      auto c1 = k_base64_table[static_cast<size_t>(in[i++])];
      auto c2 = k_base64_table[static_cast<size_t>(in[i++])];

      if (c0 == SENTINEL_VALUE || c1 == SENTINEL_VALUE ||
          c2 == SENTINEL_VALUE) {
        return "";
      }

      decoder.buffer =
          static_cast<uint32_t>(0) | static_cast<uint32_t>(c0) << 26 |
          static_cast<uint32_t>(c1) << 20 | static_cast<uint32_t>(c2) << 14;

      out.push_back(decoder.bytes[3]);
      out.push_back(decoder.bytes[2]);
    } break;

    case 2: {
      auto c0 = k_base64_table[static_cast<size_t>(in[i++])];
      auto c1 = k_base64_table[static_cast<size_t>(in[i++])];

      if (c0 == SENTINEL_VALUE || c1 == SENTINEL_VALUE) {
        return "";
      }

      decoder.buffer = static_cast<uint32_t>(0) |
                       static_cast<uint32_t>(c0) << 26 |
                       static_cast<uint32_t>(c1) << 20;

      out.push_back(decoder.bytes[3]);
    } break;
  }

  return out;
}

#undef SENTINEL_VALUE
#undef _

}  // namespace base64
}  // namespace tracing
}  // namespace datadog
