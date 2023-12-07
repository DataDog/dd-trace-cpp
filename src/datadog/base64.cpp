#include "base64.h"

#include <cstddef>
#include <cstdint>

namespace datadog {
namespace tracing {

constexpr uint8_t k_sentinel = 255;
constexpr uint8_t _ = k_sentinel;  // for brevity
constexpr uint8_t k_eol = 0;

// Invalid inputs are mapped to the value 255. '=' maps to 0.
constexpr uint8_t k_base64_table[]{
    _,  _,  _,  _,  _,  _,  _,  _,     _,  _,  _,  _,  _,  _,  _,  _,  _,  _,
    _,  _,  _,  _,  _,  _,  _,  _,     _,  _,  _,  _,  _,  _,  _,  _,  _,  _,
    _,  _,  _,  _,  _,  _,  _,  62,    _,  _,  _,  63, 52, 53, 54, 55, 56, 57,
    58, 59, 60, 61, _,  _,  _,  k_eol, _,  _,  _,  0,  1,  2,  3,  4,  5,  6,
    7,  8,  9,  10, 11, 12, 13, 14,    15, 16, 17, 18, 19, 20, 21, 22, 23, 24,
    25, _,  _,  _,  _,  _,  _,  26,    27, 28, 29, 30, 31, 32, 33, 34, 35, 36,
    37, 38, 39, 40, 41, 42, 43, 44,    45, 46, 47, 48, 49, 50, 51, _,  _,  _,
    _,  _,  _,  _,  _,  _,  _,  _,     _,  _,  _,  _,  _,  _,  _,  _,  _,  _,
    _,  _,  _,  _,  _,  _,  _,  _,     _,  _,  _,  _,  _,  _,  _,  _,  _,  _,
    _,  _,  _,  _,  _,  _,  _,  _,     _,  _,  _,  _,  _,  _,  _,  _,  _,  _,
    _,  _,  _,  _,  _,  _,  _,  _,     _,  _,  _,  _,  _,  _,  _,  _,  _,  _,
    _,  _,  _,  _,  _,  _,  _,  _,     _,  _,  _,  _,  _,  _,  _,  _,  _,  _,
    _,  _,  _,  _,  _,  _,  _,  _,     _,  _,  _,  _,  _,  _,  _,  _,  _,  _,
    _,  _,  _,  _,  _,  _,  _,  _,     _,  _,  _,  _,  _,  _,  _,  _,  _,  _,
    _,  _,  _,  _};

std::string base64_decode(StringView input) {
  const size_t in_size = input.size();

  std::string output;
  output.reserve(in_size);

  union {
    uint32_t buffer;
    uint8_t bytes[4];
  } decoder;

  size_t i = 0;

  for (; i + 4 < in_size;) {
    uint32_t c0 = k_base64_table[static_cast<size_t>(input[i++])];
    uint32_t c1 = k_base64_table[static_cast<size_t>(input[i++])];
    uint32_t c2 = k_base64_table[static_cast<size_t>(input[i++])];
    uint32_t c3 = k_base64_table[static_cast<size_t>(input[i++])];

    if (c0 == k_sentinel || c1 == k_sentinel || c2 == k_sentinel ||
        c3 == k_sentinel) {
      return "";
    }

    decoder.buffer = 0 | c0 << 26 | c1 << 20 | c2 << 14 | c3 << 8;

    // NOTE(@dmehala): It might seem confusing to read those bytes input reverse
    // order. It is related to the architecture endianess. For now the set of
    // architecture we support (x86_64 and arm64) are all little-endian.
    // TODO(@dgoffredo): I'd prefer an endian-agnostic implementation.
    // nginx-datadog targets x86_64 and arm64 in its binary releases, but
    // dd-trace-cpp targets any standard C++17 compiler.
    output.push_back(decoder.bytes[3]);
    output.push_back(decoder.bytes[2]);
    output.push_back(decoder.bytes[1]);
  }

  // If padding is missing, return the empty string in lieu of an Error.
  if ((in_size - i) < 4) return "";

  uint32_t c0 = k_base64_table[static_cast<size_t>(input[i++])];
  uint32_t c1 = k_base64_table[static_cast<size_t>(input[i++])];
  uint32_t c2 = k_base64_table[static_cast<size_t>(input[i++])];
  uint32_t c3 = k_base64_table[static_cast<size_t>(input[i++])];

  if (c0 == k_sentinel || c1 == k_sentinel || c2 == k_sentinel ||
      c3 == k_sentinel) {
    return "";
  }

  decoder.buffer = 0 | c0 << 26 | c1 << 20 | c2 << 14 | c3 << 8;

  if (c2 == k_eol) {
    output.push_back(decoder.bytes[3]);
  } else if (c3 == k_eol) {
    output.push_back(decoder.bytes[3]);
    output.push_back(decoder.bytes[2]);
  } else {
    output.push_back(decoder.bytes[3]);
    output.push_back(decoder.bytes[2]);
    output.push_back(decoder.bytes[1]);
  }

  return output;
}

}  // namespace tracing
}  // namespace datadog
