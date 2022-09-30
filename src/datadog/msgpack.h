#pragma once

#include <cstddef>
#include <cstdint>
#include <limits>
#include <stdexcept>
#include <string>
#include <string_view>
#include <type_traits>

namespace datadog {
namespace tracing {
namespace msgpack {

// First, declare all functions, so that I don't have to topologically sort
// their inline definitions.

template <typename Integer>
void push_number_big_endian(std::string& buffer, Integer integer);

template <typename Range>
void push(std::string& buffer, const Range& range);

void pack_negative(std::string& buffer, std::int64_t value);

void pack_nonnegative(std::string& buffer, std::uint64_t value);

template <typename Integer>
void pack_integer(std::string& buffer, Integer value);

void pack_double(std::string& buffer, double value);

void pack_string(std::string& buffer, const char* cstr);

template <typename Range>
void pack_string(std::string& buffer, const Range& range);

void pack_array(std::string& buffer, size_t size);

void pack_map(std::string& buffer, size_t size);

std::string make_overflow_message(std::string_view type, std::size_t actual,
                                  std::size_t max);

// MessagePack values are prefixed by a byte naming their type.
namespace types {
constexpr auto DOUBLE = std::byte(0xCB);
constexpr auto UINT64 = std::byte(0xCF);
constexpr auto INT64 = std::byte(0xD3);
constexpr auto STR32 = std::byte(0xDB);
constexpr auto ARRAY32 = std::byte(0xDD);
constexpr auto MAP32 = std::byte(0xDF);
}  // namespace types

// Here are the inline definitions of all of the functions declared above.

inline std::string make_overflow_message(std::string_view type,
                                         std::size_t actual, std::size_t max) {
  std::string message;
  message += "Cannot msgpack encode ";
  message += type;
  message += " of size ";
  message += std::to_string(actual);
  message += ", which exceeds the protocol maximum of ";
  message += std::to_string(max);
  message += '.';
  return message;
}

template <typename Integer>
void push_number_big_endian(std::string& buffer, Integer integer) {
  // Assume two's complement.
  const std::make_unsigned_t<Integer> value = integer;

  // The loop below is more likely to unroll if we don't call any functions
  // within it.
  char buf[sizeof value];

  // The most significant byte of `value` goes to the front of `buf`, and the
  // least significant byte of `value` goes
  // to the back of `buf`, and so on in between.
  // On a big endian architecture, this is just a complicated way to copy
  // `value`. On a little endian architecture, which is much more common, this
  // effectively copies the bytes of `value` backwards.
  const int size = sizeof value;
  for (int i = 0; i < size; ++i) {
    const char byte = (value >> (8 * ((size - 1) - i))) & 0xFF;
    buf[i] = byte;
  }

  buffer.append(buf, sizeof buf);
}

template <typename Range>
void push(std::string& buffer, const Range& range) {
  buffer.insert(buffer.end(), std::begin(range), std::end(range));
}

inline void pack_negative(std::string& buffer, std::int64_t value) {
  buffer.push_back(static_cast<char>(types::INT64));
  push_number_big_endian(buffer, static_cast<std::int64_t>(value));
}

inline void pack_nonnegative(std::string& buffer, std::uint64_t value) {
  buffer.push_back(static_cast<char>(types::UINT64));
  push_number_big_endian(buffer, static_cast<std::uint64_t>(value));
}

template <typename Integer>
void pack_integer(std::string& buffer, Integer value) {
  if (value < 0) {
    return pack_negative(buffer, value);
  } else {
    return pack_nonnegative(buffer, value);
  }
}

inline void pack_double(std::string& buffer, double value) {
  buffer.push_back(static_cast<char>(types::DOUBLE));

  // The following is lifted from the "msgpack-c" project.
  // See "pack_double" in
  // <https://github.com/msgpack/msgpack-c/blob/cpp_master/include/msgpack/v1/pack.hpp>

  union {
    double as_double;
    uint64_t as_integer;
  } memory;
  memory.as_double = value;

#if defined(TARGET_OS_IPHONE)
  // ok
#elif defined(__arm__) && !(__ARM_EABI__)  // arm-oabi
  // https://github.com/msgpack/msgpack-perl/pull/1
  memory.as_integer =
      (memory.as_integer & 0xFFFFFFFFUL) << 32UL | (memory.as_integer >> 32UL);
#endif

  push_number_big_endian(buffer, memory.as_integer);
}

inline void pack_string(std::string& buffer, const char* cstr) {
  return pack_string(buffer, std::string_view(cstr));
}

template <typename Range>
void pack_string(std::string& buffer, const Range& range) {
  auto size =
      static_cast<size_t>(std::distance(std::begin(range), std::end(range)));
  const auto max = std::numeric_limits<std::uint32_t>::max();
  if (size > max) {
    throw std::out_of_range(make_overflow_message("string", size, max));
  }
  buffer.push_back(static_cast<char>(types::STR32));
  push_number_big_endian(buffer, static_cast<std::uint32_t>(size));
  push(buffer, range);
}

inline void pack_array(std::string& buffer, size_t size) {
  const auto max = std::numeric_limits<std::uint32_t>::max();
  if (size > max) {
    throw std::out_of_range(make_overflow_message("array", size, max));
  }
  buffer.push_back(static_cast<char>(types::ARRAY32));
  push_number_big_endian(buffer, static_cast<std::uint32_t>(size));
}

inline void pack_map(std::string& buffer, size_t size) {
  const auto max = std::numeric_limits<std::uint32_t>::max();
  if (size > max) {
    throw std::out_of_range(make_overflow_message("map", size, max));
  }
  buffer.push_back(static_cast<char>(types::MAP32));
  push_number_big_endian(buffer, static_cast<std::uint32_t>(size));
}

}  // namespace msgpack
}  // namespace tracing
}  // namespace datadog
