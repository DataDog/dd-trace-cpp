#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>

namespace datadog {
namespace tracing {
namespace msgpack {

void pack_negative(std::string& buffer, std::int64_t value);
void pack_nonnegative(std::string& buffer, std::uint64_t value);

void pack_double(std::string& buffer, double value);
void pack_string(std::string& buffer, std::string_view value);

void pack_array(std::string& buffer, std::size_t size);
void pack_map(std::string& buffer, std::size_t size);

template <typename Entry, typename... Entries>
void pack_map_suffix(std::string& buffer, Entry&& entry, Entries&&... entries);
void pack_map_suffix(std::string& buffer);

template <typename Integer>
void pack_integer(std::string& buffer, Integer value) {
  if (value < 0) {
    return pack_negative(buffer, value);
  } else {
    return pack_nonnegative(buffer, value);
  }
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

template <typename Entry, typename... Entries>
void pack_map(std::string& buffer, Entry&& entry, Entries&&... entries) {
  pack_map(buffer, 1 + sizeof...(entries));
  pack_map_suffix(buffer, std::forward<Entry>(entry),
                  std::forward<Entries>(entries)...);
}

template <typename Entry, typename... Entries>
void pack_map_suffix(std::string& buffer, Entry&& entry, Entries&&... entries) {
  auto&& [key, pack_value] = entry;
  pack_string(buffer, key);
  pack_value(buffer);
  pack_map_suffix(buffer, std::forward<Entries>(entries)...);
}

inline void pack_map_suffix(std::string&) {}

}  // namespace msgpack
}  // namespace tracing
}  // namespace datadog
