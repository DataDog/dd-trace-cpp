#include "base64.h"

#include <cstddef>
#include <cstdint>

namespace datadog {
namespace tracing {

constexpr uint8_t k_sentinel = 255;
constexpr uint8_t _ = k_sentinel;  // for brevity
constexpr uint8_t k_eol = 0;

// Invalid inputs are mapped to the value 255. '=' maps to 0.
constexpr uint8_t k_base64_table[] = {
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

namespace {

void append_encoded_group(std::string& output, StringView group);

}  // namespace

std::string base64_decode(StringView input) {
  const size_t in_size = input.size();

  std::string output;
  output.reserve(in_size);

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

    output.push_back(static_cast<char>(c0 << 2 | (c1 & 0xF0) >> 4));
    output.push_back(static_cast<char>((c1 & 0x0F) << 4 | ((c2 & 0x3C) >> 2)));
    output.push_back(static_cast<char>(((c2 & 0x03) << 6) | (c3 & 0x3F)));
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

  if (c2 == k_eol) {
    // The last quadruplet is of the form "xx==", where only one character needs
    // to be decoded.
    output.push_back(static_cast<char>(c0 << 2 | (c1 & 0xF0) >> 4));
  } else if (c3 == k_eol) {
    // The last quadruplet is of the form "xxx=", where only two character needs
    // to be decoded.
    output.push_back(static_cast<char>(c0 << 2 | (c1 & 0xF0) >> 4));
    output.push_back(static_cast<char>((c1 & 0x0F) << 4 | ((c2 & 0x3C) >> 2)));
  } else {
    // The last quadruplet is not padded -> common use case
    output.push_back(static_cast<char>(c0 << 2 | (c1 & 0xF0) >> 4));
    output.push_back(static_cast<char>((c1 & 0x0F) << 4 | ((c2 & 0x3C) >> 2)));
    output.push_back(static_cast<char>(((c2 & 0x03) << 6) | (c3 & 0x3F)));
  }

  return output;
}

std::string base64_encode(StringView input) {
  std::string output;
  output.reserve((input.size() + 2) / 3 * 4);
  for (size_t offset = 0; offset < input.size(); offset += 3) {
    append_encoded_group(output, input.substr(offset, 3));
  }
  return output;
}

namespace {

constexpr char k_base64_alphabet[] =
    "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

// Appends the 4 characters for a group of 1 to 3 bytes. Missing bytes become
// '=' padding.
void append_encoded_group(std::string& output, StringView group) {
  uint32_t bits = 0;
  for (size_t position = 0; position < 3; ++position) {
    bits <<= 8;
    if (position < group.size()) {
      bits |= static_cast<unsigned char>(group[position]);
    }
  }
  const size_t encoded_chars = group.size() + 1;
  for (size_t position = 0; position < 4; ++position) {
    const uint32_t sextet = (bits >> (18 - 6 * position)) & 0x3F;
    output.push_back(position < encoded_chars ? k_base64_alphabet[sextet]
                                              : '=');
  }
}

}  // namespace

}  // namespace tracing
}  // namespace datadog
