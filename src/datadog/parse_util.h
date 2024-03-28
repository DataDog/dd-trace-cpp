#pragma once

// This component provides parsing-related miscellanea.

#include <cstddef>
#include <cstdint>
#include <unordered_map>
#include <vector>

#include "expected.h"
#include "string_view.h"

namespace datadog {
namespace tracing {

// Return a `string_view` over the specified range of characters `[begin, end)`.
template <typename Iterator>
StringView range(Iterator begin, Iterator end) {
  return StringView{&*begin, std::size_t(end - begin)};
}

// Remove leading and trailing whitespace (as determined by `std::isspace`) from
// the specified `input`.
StringView strip(StringView input);

// Return a non-negative integer parsed from the specified `input` with respect
// to the specified `base`, or return an `Error` if no such integer can be
// parsed. It is an error unless all of `input` is consumed by the parse.
// Leading and trailing whitespace are not ignored.
Expected<std::uint64_t> parse_uint64(StringView input, int base);
Expected<int> parse_int(StringView input, int base);

// Return a floating point number parsed from the specified `input`, or return
// an `Error` if not such number can be parsed. It is an error unless all of
// `input` is consumed by the parse. Leading and trailing whitespace are not
// ignored.
Expected<double> parse_double(StringView input);

// Return whether the specified `prefix` is a prefix of the specified `subject`.
bool starts_with(StringView subject, StringView prefix);

// Convert the specified `text` to lower case in-place.
void to_lower(std::string& text);

// List items are separated by an optional comma (",") and any amount of
// whitespace.
// Leading and trailing whitespace are ignored.
std::vector<StringView> parse_list(StringView input);

Expected<std::unordered_map<std::string, std::string>> parse_tags(
    const std::vector<StringView>& list);
Expected<std::unordered_map<std::string, std::string>> parse_tags(
    const std::vector<std::string>& list);

Expected<std::unordered_map<std::string, std::string>> parse_tags(
    StringView input);

}  // namespace tracing
}  // namespace datadog
