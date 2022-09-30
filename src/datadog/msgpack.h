#pragma once

// This component provides encoding routines for [MessagePack][1].
//
// Each function is in `namespace msgpack` and appends a specified value to a
// `std::string`.  For example, `msgpack::pack_integer(destination, -42)`
// MessagePack encodes the number `-42` and appends the result to `destination`.
//
// Only encoding is provided, and only for the types required by `SpanData` and
// `DatadogAgent`.
//
// [1]: https://msgpack.org/index.html

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <utility>

#include "expected.h"

namespace datadog {
namespace tracing {
namespace msgpack {

void pack_integer(std::string& buffer, std::int64_t value);
void pack_integer(std::string& buffer, std::uint64_t value);

inline void pack_integer(std::string& buffer, std::int32_t value) {
  pack_integer(buffer, std::int64_t(value));
}

void pack_double(std::string& buffer, double value);
void pack_string(std::string& buffer, std::string_view value);

void pack_array(std::string& buffer, std::size_t size);

// Append to the specified `buffer` a MessagePack encoded array having the
// specified `values`, where for each element of `values` the specified
// `pack_value` function appends the value.  `pack_value` is invoked with two
// arguments: the first is a reference to `buffer`, and the second is a
// reference to the current value.  `pack_value` returns an `Expected<void>`.
// If the return value is an error, then iteration is halted and the error is
// returned.  Otherwise the non-error value is returned.
template <typename Iterable, typename PackValue>
Expected<void> pack_array(std::string& buffer, Iterable&& values,
                          PackValue&& pack_value);

void pack_map(std::string& buffer, std::size_t size);

// Append to the specified `buffer` a MessagePack encoded map consisting of the
// specified `pairs`, where the first element of each pair is the name of the
// map element, and the second element of each pair is some value that is
// MessagePack encoded by the specified `pack_value` function. `pack_value` is
// invoked with two arguments: the first is a reference to `buffer`, and the
// second is a reference to the current value.
template <typename PairIterable, typename PackValue>
void pack_map(std::string& buffer, const PairIterable& pairs,
              PackValue&& pack_value);

// Append to the specified `buffer` a MessagePack encoded map consisting of the
// specified key value pairs.  After the `buffer` argument, `pack_map` accepts
// an even number of arguments.  First in each pair of arguments is `key`, the
// key name of the corresponding map item.  Second in each pair of arguments is
// `pack_value`, a function that encodes the corresponding value.  `pack_value`
// is invoked with two arguments: the first is a reference to `buffer`, and the
// second is a reference to the current value.
template <typename Key, typename PackValue, typename... Rest>
void pack_map(std::string& buffer, Key&& key, PackValue&& pack_value,
              Rest&&... rest);

template <typename Key, typename PackValue, typename... Rest>
void pack_map_suffix(std::string& buffer, Key&& key, PackValue&& pack_value,
                     Rest&&... rest);
void pack_map_suffix(std::string& buffer);

template <typename Iterable, typename PackValue>
Expected<void> pack_array(std::string& buffer, Iterable&& values,
                          PackValue&& pack_value) {
  Expected<void> result;
  pack_array(buffer, std::size(values));
  for (const auto& value : values) {
    result = pack_value(buffer, value);
    if (!result) {
      break;
    }
  }
  return result;
}

template <typename PairIterable, typename PackValue>
void pack_map(std::string& buffer, const PairIterable& pairs,
              PackValue&& pack_value) {
  pack_map(buffer, std::size(pairs));
  for (const auto& [key, value] : pairs) {
    pack_string(buffer, key);
    pack_value(buffer, value);
  }
}

template <typename Key, typename PackValue, typename... Rest>
void pack_map(std::string& buffer, Key&& key, PackValue&& pack_value,
              Rest&&... rest) {
  pack_map(buffer, 1 + sizeof...(rest));
  pack_map_suffix(buffer, std::forward<Key>(key),
                  std::forward<PackValue>(pack_value),
                  std::forward<Rest>(rest)...);
}

template <typename Key, typename PackValue, typename... Rest>
void pack_map_suffix(std::string& buffer, Key&& key, PackValue&& pack_value,
                     Rest&&... rest) {
  pack_string(buffer, key);
  pack_value(buffer);
  pack_map_suffix(buffer, std::forward<Rest>(rest)...);
}

inline void pack_map_suffix(std::string&) {
  // base case does nothing
}

}  // namespace msgpack
}  // namespace tracing
}  // namespace datadog
