#pragma once

// This code is based off of
// <https://github.com/ousttrue/msgpackpp/blob/b7a08ac9e56a37499b274039ea2b454fa4be0c5b/msgpackpp/include/msgpackpp/msgpackpp.h>
// from ousttrue's "msgpackpp"
// <https://github.com/ousttrue/msgpackpp>
//
// I removed all but the `packer` code, and made some modifications:
// - `class packer` no longer stores its output -- it takes a pointer.
// - Encoding no longer assumes that the host platform is little endian.

#include <cstdint>
#include <limits>
#include <stdexcept>
#include <string_view>
#include <type_traits>

namespace msgpackpp {

struct pack_error : public std::runtime_error {
  pack_error(const char *msg) : runtime_error(msg) {}
};

struct overflow_pack_error : public pack_error {
  overflow_pack_error() : pack_error("overflow") {}
};

enum pack_type : std::uint8_t {
  POSITIVE_FIXNUM = 0x00,
  POSITIVE_FIXNUM_0x01 = 0x01,
  POSITIVE_FIXNUM_0x02 = 0x02,
  POSITIVE_FIXNUM_0x03 = 0x03,
  POSITIVE_FIXNUM_0x04 = 0x04,
  POSITIVE_FIXNUM_0x05 = 0x05,
  POSITIVE_FIXNUM_0x06 = 0x06,
  POSITIVE_FIXNUM_0x07 = 0x07,
  POSITIVE_FIXNUM_0x08 = 0x08,
  POSITIVE_FIXNUM_0x09 = 0x09,
  POSITIVE_FIXNUM_0x0A = 0x0A,
  POSITIVE_FIXNUM_0x0B = 0x0B,
  POSITIVE_FIXNUM_0x0C = 0x0C,
  POSITIVE_FIXNUM_0x0D = 0x0D,
  POSITIVE_FIXNUM_0x0E = 0x0E,
  POSITIVE_FIXNUM_0x0F = 0x0F,

  POSITIVE_FIXNUM_0x10 = 0x10,
  POSITIVE_FIXNUM_0x11 = 0x11,
  POSITIVE_FIXNUM_0x12 = 0x12,
  POSITIVE_FIXNUM_0x13 = 0x13,
  POSITIVE_FIXNUM_0x14 = 0x14,
  POSITIVE_FIXNUM_0x15 = 0x15,
  POSITIVE_FIXNUM_0x16 = 0x16,
  POSITIVE_FIXNUM_0x17 = 0x17,
  POSITIVE_FIXNUM_0x18 = 0x18,
  POSITIVE_FIXNUM_0x19 = 0x19,
  POSITIVE_FIXNUM_0x1A = 0x1A,
  POSITIVE_FIXNUM_0x1B = 0x1B,
  POSITIVE_FIXNUM_0x1C = 0x1C,
  POSITIVE_FIXNUM_0x1D = 0x1D,
  POSITIVE_FIXNUM_0x1E = 0x1E,
  POSITIVE_FIXNUM_0x1F = 0x1F,

  POSITIVE_FIXNUM_0x20 = 0x20,
  POSITIVE_FIXNUM_0x21 = 0x21,
  POSITIVE_FIXNUM_0x22 = 0x22,
  POSITIVE_FIXNUM_0x23 = 0x23,
  POSITIVE_FIXNUM_0x24 = 0x24,
  POSITIVE_FIXNUM_0x25 = 0x25,
  POSITIVE_FIXNUM_0x26 = 0x26,
  POSITIVE_FIXNUM_0x27 = 0x27,
  POSITIVE_FIXNUM_0x28 = 0x28,
  POSITIVE_FIXNUM_0x29 = 0x29,
  POSITIVE_FIXNUM_0x2A = 0x2A,
  POSITIVE_FIXNUM_0x2B = 0x2B,
  POSITIVE_FIXNUM_0x2C = 0x2C,
  POSITIVE_FIXNUM_0x2D = 0x2D,
  POSITIVE_FIXNUM_0x2E = 0x2E,
  POSITIVE_FIXNUM_0x2F = 0x2F,

  POSITIVE_FIXNUM_0x30 = 0x30,
  POSITIVE_FIXNUM_0x31 = 0x31,
  POSITIVE_FIXNUM_0x32 = 0x32,
  POSITIVE_FIXNUM_0x33 = 0x33,
  POSITIVE_FIXNUM_0x34 = 0x34,
  POSITIVE_FIXNUM_0x35 = 0x35,
  POSITIVE_FIXNUM_0x36 = 0x36,
  POSITIVE_FIXNUM_0x37 = 0x37,
  POSITIVE_FIXNUM_0x38 = 0x38,
  POSITIVE_FIXNUM_0x39 = 0x39,
  POSITIVE_FIXNUM_0x3A = 0x3A,
  POSITIVE_FIXNUM_0x3B = 0x3B,
  POSITIVE_FIXNUM_0x3C = 0x3C,
  POSITIVE_FIXNUM_0x3D = 0x3D,
  POSITIVE_FIXNUM_0x3E = 0x3E,
  POSITIVE_FIXNUM_0x3F = 0x3F,

  POSITIVE_FIXNUM_0x40 = 0x40,
  POSITIVE_FIXNUM_0x41 = 0x41,
  POSITIVE_FIXNUM_0x42 = 0x42,
  POSITIVE_FIXNUM_0x43 = 0x43,
  POSITIVE_FIXNUM_0x44 = 0x44,
  POSITIVE_FIXNUM_0x45 = 0x45,
  POSITIVE_FIXNUM_0x46 = 0x46,
  POSITIVE_FIXNUM_0x47 = 0x47,
  POSITIVE_FIXNUM_0x48 = 0x48,
  POSITIVE_FIXNUM_0x49 = 0x49,
  POSITIVE_FIXNUM_0x4A = 0x4A,
  POSITIVE_FIXNUM_0x4B = 0x4B,
  POSITIVE_FIXNUM_0x4C = 0x4C,
  POSITIVE_FIXNUM_0x4D = 0x4D,
  POSITIVE_FIXNUM_0x4E = 0x4E,
  POSITIVE_FIXNUM_0x4F = 0x4F,

  POSITIVE_FIXNUM_0x50 = 0x50,
  POSITIVE_FIXNUM_0x51 = 0x51,
  POSITIVE_FIXNUM_0x52 = 0x52,
  POSITIVE_FIXNUM_0x53 = 0x53,
  POSITIVE_FIXNUM_0x54 = 0x54,
  POSITIVE_FIXNUM_0x55 = 0x55,
  POSITIVE_FIXNUM_0x56 = 0x56,
  POSITIVE_FIXNUM_0x57 = 0x57,
  POSITIVE_FIXNUM_0x58 = 0x58,
  POSITIVE_FIXNUM_0x59 = 0x59,
  POSITIVE_FIXNUM_0x5A = 0x5A,
  POSITIVE_FIXNUM_0x5B = 0x5B,
  POSITIVE_FIXNUM_0x5C = 0x5C,
  POSITIVE_FIXNUM_0x5D = 0x5D,
  POSITIVE_FIXNUM_0x5E = 0x5E,
  POSITIVE_FIXNUM_0x5F = 0x5F,

  POSITIVE_FIXNUM_0x60 = 0x60,
  POSITIVE_FIXNUM_0x61 = 0x61,
  POSITIVE_FIXNUM_0x62 = 0x62,
  POSITIVE_FIXNUM_0x63 = 0x63,
  POSITIVE_FIXNUM_0x64 = 0x64,
  POSITIVE_FIXNUM_0x65 = 0x65,
  POSITIVE_FIXNUM_0x66 = 0x66,
  POSITIVE_FIXNUM_0x67 = 0x67,
  POSITIVE_FIXNUM_0x68 = 0x68,
  POSITIVE_FIXNUM_0x69 = 0x69,
  POSITIVE_FIXNUM_0x6A = 0x6A,
  POSITIVE_FIXNUM_0x6B = 0x6B,
  POSITIVE_FIXNUM_0x6C = 0x6C,
  POSITIVE_FIXNUM_0x6D = 0x6D,
  POSITIVE_FIXNUM_0x6E = 0x6E,
  POSITIVE_FIXNUM_0x6F = 0x6F,

  POSITIVE_FIXNUM_0x70 = 0x70,
  POSITIVE_FIXNUM_0x71 = 0x71,
  POSITIVE_FIXNUM_0x72 = 0x72,
  POSITIVE_FIXNUM_0x73 = 0x73,
  POSITIVE_FIXNUM_0x74 = 0x74,
  POSITIVE_FIXNUM_0x75 = 0x75,
  POSITIVE_FIXNUM_0x76 = 0x76,
  POSITIVE_FIXNUM_0x77 = 0x77,
  POSITIVE_FIXNUM_0x78 = 0x78,
  POSITIVE_FIXNUM_0x79 = 0x79,
  POSITIVE_FIXNUM_0x7A = 0x7A,
  POSITIVE_FIXNUM_0x7B = 0x7B,
  POSITIVE_FIXNUM_0x7C = 0x7C,
  POSITIVE_FIXNUM_0x7D = 0x7D,
  POSITIVE_FIXNUM_0x7E = 0x7E,
  POSITIVE_FIXNUM_0x7F = 0x7F,

  FIX_MAP = 0x80,
  FIX_MAP_0x1 = 0x81,
  FIX_MAP_0x2 = 0x82,
  FIX_MAP_0x3 = 0x83,
  FIX_MAP_0x4 = 0x84,
  FIX_MAP_0x5 = 0x85,
  FIX_MAP_0x6 = 0x86,
  FIX_MAP_0x7 = 0x87,
  FIX_MAP_0x8 = 0x88,
  FIX_MAP_0x9 = 0x89,
  FIX_MAP_0xA = 0x8A,
  FIX_MAP_0xB = 0x8B,
  FIX_MAP_0xC = 0x8C,
  FIX_MAP_0xD = 0x8D,
  FIX_MAP_0xE = 0x8E,
  FIX_MAP_0xF = 0x8F,

  FIX_ARRAY = 0x90,
  FIX_ARRAY_0x1 = 0x91,
  FIX_ARRAY_0x2 = 0x92,
  FIX_ARRAY_0x3 = 0x93,
  FIX_ARRAY_0x4 = 0x94,
  FIX_ARRAY_0x5 = 0x95,
  FIX_ARRAY_0x6 = 0x96,
  FIX_ARRAY_0x7 = 0x97,
  FIX_ARRAY_0x8 = 0x98,
  FIX_ARRAY_0x9 = 0x99,
  FIX_ARRAY_0xA = 0x9A,
  FIX_ARRAY_0xB = 0x9B,
  FIX_ARRAY_0xC = 0x9C,
  FIX_ARRAY_0xD = 0x9D,
  FIX_ARRAY_0xE = 0x9E,
  FIX_ARRAY_0xF = 0x9F,

  FIX_STR = 0xA0,
  FIX_STR_0x01 = 0xA1,
  FIX_STR_0x02 = 0xA2,
  FIX_STR_0x03 = 0xA3,
  FIX_STR_0x04 = 0xA4,
  FIX_STR_0x05 = 0xA5,
  FIX_STR_0x06 = 0xA6,
  FIX_STR_0x07 = 0xA7,
  FIX_STR_0x08 = 0xA8,
  FIX_STR_0x09 = 0xA9,
  FIX_STR_0x0A = 0xAA,
  FIX_STR_0x0B = 0xAB,
  FIX_STR_0x0C = 0xAC,
  FIX_STR_0x0D = 0xAD,
  FIX_STR_0x0E = 0xAE,
  FIX_STR_0x0F = 0xAF,
  FIX_STR_0x10 = 0xB0,
  FIX_STR_0x11 = 0xB1,
  FIX_STR_0x12 = 0xB2,
  FIX_STR_0x13 = 0xB3,
  FIX_STR_0x14 = 0xB4,
  FIX_STR_0x15 = 0xB5,
  FIX_STR_0x16 = 0xB6,
  FIX_STR_0x17 = 0xB7,
  FIX_STR_0x18 = 0xB8,
  FIX_STR_0x19 = 0xB9,
  FIX_STR_0x1A = 0xBA,
  FIX_STR_0x1B = 0xBB,
  FIX_STR_0x1C = 0xBC,
  FIX_STR_0x1D = 0xBD,
  FIX_STR_0x1E = 0xBE,
  FIX_STR_0x1F = 0xBF,

  NIL = 0xC0,
  NEVER_USED = 0xC1,
  False = 0xC2,  // avoid match windows False
  True = 0xC3,   // avoid match windows True

  BIN8 = 0xC4,
  BIN16 = 0xC5,
  BIN32 = 0xC6,

  EXT8 = 0xC7,
  EXT16 = 0xC8,
  EXT32 = 0xC9,

  FLOAT = 0xCA,
  DOUBLE = 0xCB,
  UINT8 = 0xCC,
  UINT16 = 0xCD,
  UINT32 = 0xCE,
  UINT64 = 0xCF,
  INT8 = 0xD0,
  INT16 = 0xD1,
  INT32 = 0xD2,
  INT64 = 0xD3,

  FIX_EXT_1 = 0xD4,
  FIX_EXT_2 = 0xD5,
  FIX_EXT_4 = 0xD6,
  FIX_EXT_8 = 0xD7,
  FIX_EXT_16 = 0xD8,

  STR8 = 0xD9,
  STR16 = 0xDA,
  STR32 = 0xDB,

  ARRAY16 = 0xDC,
  ARRAY32 = 0xDD,
  MAP16 = 0xDE,
  MAP32 = 0xDF,

  NEGATIVE_FIXNUM = 0xE0,       // 1110 0000 = -32
  NEGATIVE_FIXNUM_0x1F = 0xE1,  // -31
  NEGATIVE_FIXNUM_0x1E = 0xE2,
  NEGATIVE_FIXNUM_0x1D = 0xE3,
  NEGATIVE_FIXNUM_0x1C = 0xE4,
  NEGATIVE_FIXNUM_0x1B = 0xE5,
  NEGATIVE_FIXNUM_0x1A = 0xE6,
  NEGATIVE_FIXNUM_0x19 = 0xE7,
  NEGATIVE_FIXNUM_0x18 = 0xE8,
  NEGATIVE_FIXNUM_0x17 = 0xE9,
  NEGATIVE_FIXNUM_0x16 = 0xEA,
  NEGATIVE_FIXNUM_0x15 = 0xEB,
  NEGATIVE_FIXNUM_0x14 = 0xEC,
  NEGATIVE_FIXNUM_0x13 = 0xED,
  NEGATIVE_FIXNUM_0x12 = 0xEE,
  NEGATIVE_FIXNUM_0x11 = 0xEF,
  NEGATIVE_FIXNUM_0x10 = 0xF0,
  NEGATIVE_FIXNUM_0x0F = 0xF1,
  NEGATIVE_FIXNUM_0x0E = 0xF2,
  NEGATIVE_FIXNUM_0x0D = 0xF3,
  NEGATIVE_FIXNUM_0x0C = 0xF4,
  NEGATIVE_FIXNUM_0x0B = 0xF5,
  NEGATIVE_FIXNUM_0x0A = 0xF6,
  NEGATIVE_FIXNUM_0x09 = 0xF7,
  NEGATIVE_FIXNUM_0x08 = 0xF8,
  NEGATIVE_FIXNUM_0x07 = 0xF9,
  NEGATIVE_FIXNUM_0x06 = 0xFA,
  NEGATIVE_FIXNUM_0x05 = 0xFB,
  NEGATIVE_FIXNUM_0x04 = 0xFC,
  NEGATIVE_FIXNUM_0x03 = 0xFD,
  NEGATIVE_FIXNUM_0x02 = 0xFE,
  NEGATIVE_FIXNUM_0x01 = 0xFF,  // -1
};

template <typename OutputBuffer>
class packer {
  OutputBuffer *m_buffer;

 private:
  template <typename Integer>
  void push_number_big_endian(Integer integer) {
    // Assume two's complement.
    const std::make_unsigned_t<Integer> value = integer;

    // The loop below is more likely to unroll if we don't call any functions
    // within it.
    char buf[sizeof value];

    const int size = sizeof value;
    for (int i = 0; i < size; ++i) {
      const char byte = (value >> (8 * ((size - 1) - i))) & 0xFF;
      buf[i] = byte;
    }

    m_buffer->append(buf, sizeof buf);
  }

 public:
  packer(OutputBuffer *output_buffer) : m_buffer(output_buffer) {}

  packer() = delete;
  packer(const packer &) = delete;
  packer &operator=(const packer &) = delete;

  template <class Range>
  void push(const Range &r) {
    m_buffer->insert(m_buffer->end(), std::begin(r), std::end(r));
  }

  packer &pack_nil() {
    m_buffer->push_back(static_cast<char>(pack_type::NIL));
    return *this;
  }

  packer &pack_negative(std::int64_t n) {
    if (n >= -32) {
      m_buffer->push_back(pack_type::NEGATIVE_FIXNUM |
                          static_cast<std::uint8_t>((n + 32)));
    } else if (n >= std::numeric_limits<std::int8_t>::min()) {
      m_buffer->push_back(static_cast<char>(pack_type::INT8));
      m_buffer->push_back(static_cast<char>(n));
    } else if (n >= std::numeric_limits<std::int16_t>::min()) {
      m_buffer->push_back(static_cast<char>(pack_type::INT16));
      // network byteorder
      push_number_big_endian(static_cast<std::int16_t>(n));
    } else if (n >= std::numeric_limits<std::int32_t>::min()) {
      m_buffer->push_back(static_cast<char>(pack_type::INT32));
      // network byteorder
      push_number_big_endian(static_cast<std::int32_t>(n));
    } else if (n >= std::numeric_limits<std::int64_t>::min()) {
      m_buffer->push_back(static_cast<char>(pack_type::INT64));
      // network byteorder
      push_number_big_endian(static_cast<std::int64_t>(n));
    }

    return *this;
  }

  packer &pack_nonnegative(std::uint64_t n) {
    if (n <= 0x7F) {
      m_buffer->push_back(static_cast<char>(static_cast<std::uint8_t>(n)));
    } else if (n <= std::numeric_limits<std::uint8_t>::max()) {
      m_buffer->push_back(static_cast<char>(pack_type::UINT8));
      m_buffer->push_back(static_cast<char>(static_cast<std::uint8_t>(n)));
    } else if (n <= std::numeric_limits<std::uint16_t>::max()) {
      m_buffer->push_back(static_cast<char>(pack_type::UINT16));
      // network byteorder
      push_number_big_endian(static_cast<std::uint16_t>(n));
    } else if (n <= std::numeric_limits<std::uint32_t>::max()) {
      m_buffer->push_back(static_cast<char>(pack_type::UINT32));
      // network byteorder
      push_number_big_endian(static_cast<std::uint32_t>(n));
    } else if (n <= std::numeric_limits<std::uint64_t>::max()) {
      m_buffer->push_back(static_cast<char>(pack_type::UINT64));
      // network byteorder
      push_number_big_endian(static_cast<std::uint64_t>(n));
    }

    return *this;
  }

  template <typename T>
  packer &pack_integer(T n) {
    if (n < 0) {
      return pack_negative(n);
    } else {
      return pack_nonnegative(n);
    }
  }

  packer &pack_double(double n) {
    m_buffer->push_back(static_cast<char>(pack_type::DOUBLE));

    // The following is lifted from the "msgpack-c" project.
    // See "pack_double" in
    // <https://github.com/msgpack/msgpack-c/blob/cpp_master/include/msgpack/v1/pack.hpp>

    union {
      double as_double;
      uint64_t as_integer;
    } memory;
    memory.as_double = n;

#if defined(TARGET_OS_IPHONE)
    // ok
#elif defined(__arm__) && !(__ARM_EABI__)  // arm-oabi
    // https://github.com/msgpack/msgpack-perl/pull/1
    memory.as_integer = (memory.as_integer & 0xFFFFFFFFUL) << 32UL |
                        (memory.as_integer >> 32UL);
#endif

    push_number_big_endian(memory.as_integer);
    return *this;
  }

  packer &pack_bool(bool isTrue) {
    if (isTrue) {
      m_buffer->push_back(static_cast<char>(pack_type::True));
    } else {
      m_buffer->push_back(static_cast<char>(pack_type::False));
    }
    return *this;
  }

  packer &pack_str(const char *cstr) {
    return pack_str(std::string_view(cstr));
  }

  template <class Range>
  packer &pack_str(const Range &r) {
    auto size = static_cast<size_t>(std::distance(std::begin(r), std::end(r)));
    if (size < 32) {
      m_buffer->push_back(static_cast<char>(pack_type::FIX_STR |
                                            static_cast<std::uint8_t>(size)));
      push(r);
    } else if (size <= std::numeric_limits<std::uint8_t>::max()) {
      m_buffer->push_back(static_cast<char>(pack_type::STR8));
      m_buffer->push_back(static_cast<char>(static_cast<std::uint8_t>(size)));
      push(r);
    } else if (size <= std::numeric_limits<std::uint16_t>::max()) {
      m_buffer->push_back(static_cast<char>(pack_type::STR16));
      push_number_big_endian(static_cast<std::uint16_t>(size));
      push(r);
    } else if (size <= std::numeric_limits<std::uint32_t>::max()) {
      m_buffer->push_back(static_cast<char>(pack_type::STR32));
      push_number_big_endian(static_cast<std::uint32_t>(size));
      push(r);
    } else {
      throw overflow_pack_error();
    }
    return *this;
  }

  template <class Range>
  packer &pack_bin(const Range &r) {
    auto size = static_cast<size_t>(std::distance(std::begin(r), std::end(r)));
    if (size <= std::numeric_limits<std::uint8_t>::max()) {
      m_buffer->push_back(static_cast<char>(pack_type::BIN8));
      m_buffer->push_back(static_cast<char>(static_cast<std::uint8_t>(size)));
      push(r);
    } else if (size <= std::numeric_limits<std::uint16_t>::max()) {
      m_buffer->push_back(static_cast<char>(pack_type::BIN16));
      push_number_big_endian(static_cast<std::uint16_t>(size));
      push(r);
    } else if (size <= std::numeric_limits<std::uint32_t>::max()) {
      m_buffer->push_back(static_cast<char>(pack_type::BIN32));
      push_number_big_endian(static_cast<std::uint32_t>(size));
      push(r);
    } else {
      throw overflow_pack_error();
    }
    return *this;
  }

  packer &pack_array(size_t n) {
    if (n <= 15) {
      m_buffer->push_back(static_cast<char>(pack_type::FIX_ARRAY |
                                            static_cast<std::uint8_t>(n)));
    } else if (n <= std::numeric_limits<std::uint16_t>::max()) {
      m_buffer->push_back(static_cast<char>(pack_type::ARRAY16));
      push_number_big_endian(static_cast<std::uint16_t>(n));
    } else if (n <= std::numeric_limits<std::uint32_t>::max()) {
      m_buffer->push_back(static_cast<char>(pack_type::ARRAY32));
      push_number_big_endian(static_cast<std::uint32_t>(n));
    } else {
      throw overflow_pack_error();
    }
    return *this;
  }

  packer &pack_map(size_t n) {
    if (n <= 15) {
      m_buffer->push_back(
          static_cast<char>(pack_type::FIX_MAP | static_cast<std::uint8_t>(n)));
    } else if (n <= std::numeric_limits<std::uint16_t>::max()) {
      m_buffer->push_back(static_cast<char>(pack_type::MAP16));
      push_number_big_endian(static_cast<std::uint16_t>(n));
    } else if (n <= std::numeric_limits<std::uint32_t>::max()) {
      m_buffer->push_back(static_cast<char>(pack_type::MAP32));
      push_number_big_endian(static_cast<std::uint32_t>(n));
    } else {
      throw overflow_pack_error();
    }
    return *this;
  }

  template <class Range>
  packer &pack_ext(char type, const Range &r) {
    auto size = static_cast<size_t>(std::distance(std::begin(r), std::end(r)));

    // FIXEXT
    switch (size) {
      case 1:
        m_buffer->push_back(static_cast<char>(pack_type::FIX_EXT_1));
        m_buffer->push_back(static_cast<char>(type));
        push(r);
        break;

      case 2:
        m_buffer->push_back(static_cast<char>(pack_type::FIX_EXT_2));
        m_buffer->push_back(static_cast<char>(type));
        push(r);
        break;

      case 4:
        m_buffer->push_back(static_cast<char>(pack_type::FIX_EXT_4));
        m_buffer->push_back(static_cast<char>(type));
        push(r);
        break;

      case 8:
        m_buffer->push_back(static_cast<char>(pack_type::FIX_EXT_8));
        m_buffer->push_back(static_cast<char>(type));
        push(r);
        break;

      case 16:
        m_buffer->push_back(static_cast<char>(pack_type::FIX_EXT_16));
        m_buffer->push_back(static_cast<char>(type));
        push(r);
        break;

      default:
        // EXT
        if (size <= std::numeric_limits<std::uint8_t>::max()) {
          m_buffer->push_back(static_cast<char>(pack_type::EXT8));
          m_buffer->push_back(
              static_cast<char>(static_cast<std::uint8_t>(size)));
          m_buffer->push_back(static_cast<char>(type));
          push(r);
        } else if (size <= std::numeric_limits<std::uint16_t>::max()) {
          m_buffer->push_back(static_cast<char>(pack_type::EXT16));
          push_number_big_endian(static_cast<std::uint16_t>(size));
          m_buffer->push_back(static_cast<char>(type));
          push(r);
        } else if (size <= std::numeric_limits<std::uint32_t>::max()) {
          m_buffer->push_back(static_cast<char>(pack_type::EXT32));
          push_number_big_endian(static_cast<std::uint32_t>(size));
          m_buffer->push_back(static_cast<char>(type));
          push(r);
        } else {
          throw overflow_pack_error();
        }
    }

    return *this;
  }
};

}  // namespace msgpackpp
