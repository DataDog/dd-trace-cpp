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
  FIX_MAP = 0x80,

  FIX_ARRAY = 0x90,

  FIX_STR = 0xA0,

  NIL = 0xC0,
  FALSE = 0xC2,
  TRUE = 0xC3,

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

  NEGATIVE_FIXNUM = 0xE0,  // 1110 0000 = -32
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
      m_buffer->push_back(static_cast<char>(pack_type::TRUE));
    } else {
      m_buffer->push_back(static_cast<char>(pack_type::FALSE));
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
