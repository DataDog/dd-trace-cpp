#include "tag_propagation.h"

#include <algorithm>
#include <cstddef>
#include <sstream>

#include "error.h"
#include "parse_util.h"

namespace datadog {
namespace tracing {

// The following [eBNF][1] grammar describes the tag propagation encoding.
// The grammar was copied from [an internal design document][2].
//
//     tagset = ( tag, { ",", tag } ) | "";
//     tag = ( identifier - space or equal ), "=", identifier;
//     identifier = allowed characters, { allowed characters };
//     allowed characters = ( ? ASCII characters 32-126 ? - "," );
//     space or equal = " " | "=";
//
// That is, comma-separated "<key>=<value>" pairs.
//
// See `tag_propagation_test.cpp` for examples.
//
// [1]: https://en.wikipedia.org/wiki/Extended_Backus%E2%80%93Naur_form
// [2]:
// https://docs.google.com/document/d/1zeO6LGnvxk5XweObHAwJbK3SfK23z7jQzp7ozWJTa2A/edit#heading=h.yp07yuixga36

namespace {

// Insert into the specified `destination` a tag decoded from the specified
// `entry`.  Return an `Error` if an error occurs.
Expected<void> decode_tag(
    std::vector<std::pair<std::string, std::string>>& destination,
    StringView entry) {
  const auto separator = std::find(entry.begin(), entry.end(), '=');
  if (separator == entry.end()) {
    std::string message;
    message += "invalid key=value pair for encoded tag: missing \"=\" in: ";
    append(message, entry);
    return Error{Error::MALFORMED_TRACE_TAGS, std::move(message)};
  }

  const StringView key = range(entry.begin(), separator);
  const StringView value = range(separator + 1, entry.end());
  destination.emplace_back(std::string(key), std::string(value));

  return nullopt;
}

void append_tag(std::string& serialized_tags, StringView tag_key,
                StringView tag_value) {
  serialized_tags.append(tag_key.begin(), tag_key.end());
  serialized_tags += '=';
  serialized_tags.append(tag_value.begin(), tag_value.end());
}

}  // namespace

Expected<std::vector<std::pair<std::string, std::string>>> decode_tags(
    StringView header_value) {
  std::vector<std::pair<std::string, std::string>> tags;

  auto iter = header_value.begin();
  const auto end = header_value.end();
  if (iter == end) {
    // An empty string means no tags.
    return tags;
  }

  decltype(iter) next;
  do {
    next = std::find(iter, end, ',');
    auto result = decode_tag(tags, range(iter, next));
    if (auto* error = result.if_error()) {
      std::string prefix;
      prefix += "Error decoding trace tags \"";
      append(prefix, header_value);
      prefix += "\": ";
      return error->with_prefix(prefix);
    }
    iter = next + 1;
  } while (next != end);

  return tags;
}

std::string encode_tags(
    const std::vector<std::pair<std::string, std::string>>& trace_tags) {
  std::string result;
  auto iter = trace_tags.begin();
  if (iter == trace_tags.end()) {
    return result;
  }

  append_tag(result, iter->first, iter->second);
  for (++iter; iter != trace_tags.end(); ++iter) {
    result += ',';
    append_tag(result, iter->first, iter->second);
  }

  return result;
}

}  // namespace tracing
}  // namespace datadog
