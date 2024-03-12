#include "parse_util.h"

#include <algorithm>
#include <cassert>
#include <cctype>
#include <charconv>
#include <iterator>
#include <sstream>
#include <string>

#include "error.h"

namespace datadog {
namespace tracing {
namespace {

template <typename Integer>
Expected<Integer> parse_integer(StringView input, int base, StringView kind) {
  Integer value;
  const auto status = std::from_chars(input.begin(), input.end(), value, base);
  if (status.ec == std::errc::invalid_argument) {
    std::string message;
    message += "Is not a valid integer: \"";
    append(message, input);
    message += '\"';
    return Error{Error::INVALID_INTEGER, std::move(message)};
  } else if (status.ptr != input.end()) {
    std::string message;
    message += "Integer has trailing characters in: \"";
    append(message, input);
    message += '\"';
    return Error{Error::INVALID_INTEGER, std::move(message)};
  } else if (status.ec == std::errc::result_out_of_range) {
    std::string message;
    message += "Integer is not within the range of ";
    append(message, kind);
    message += ": ";
    append(message, input);
    return Error{Error::OUT_OF_RANGE_INTEGER, std::move(message)};
  }
  return value;
}

}  // namespace

StringView strip(StringView input) {
  const auto not_whitespace = [](unsigned char ch) {
    return !std::isspace(ch);
  };
  const char *const begin =
      std::find_if(input.begin(), input.end(), not_whitespace);
  const char *const end =
      std::find_if(input.rbegin(), std::make_reverse_iterator(begin),
                   not_whitespace)
          .base();

  assert(begin <= end);

  return StringView{begin, std::size_t(end - begin)};
}

Expected<std::uint64_t> parse_uint64(StringView input, int base) {
  return parse_integer<std::uint64_t>(input, base, "64-bit unsigned");
}

Expected<int> parse_int(StringView input, int base) {
  return parse_integer<int>(input, base, "int");
}

Expected<double> parse_double(StringView input) {
  // This function uses a different technique from `parse_integer`, because
  // some compilers with _partial_ support for C++17 do not implement the
  // floating point portions of `std::from_chars`:
  // <https://en.cppreference.com/w/cpp/compiler_support/17#C.2B.2B17_library_features>.
  // As an alternative, we could use either of `std::stod` or `std::istream`.
  // I choose `std::istream`.
  double value;
  std::stringstream stream;
  stream << input;
  stream >> value;

  if (!stream) {
    std::string message;
    message +=
        "Is not a valid number, or is out of the range of double precision "
        "floating point: \"";
    append(message, input);
    message += '\"';
    return Error{Error::INVALID_DOUBLE, std::move(message)};
  } else if (!stream.eof()) {
    std::string message;
    message += "Number has trailing characters in: \"";
    append(message, input);
    message += '\"';
    return Error{Error::INVALID_DOUBLE, std::move(message)};
  }

  return value;
}

bool starts_with(StringView subject, StringView prefix) {
  if (prefix.size() > subject.size()) {
    return false;
  }

  return std::mismatch(subject.begin(), subject.end(), prefix.begin()).second ==
         prefix.end();
}

void to_lower(std::string &text) {
  std::transform(text.begin(), text.end(), text.begin(),
                 [](unsigned char ch) { return std::tolower(ch); });
}

// List items are separated by an optional comma (",") and any amount of
// whitespace.
// Leading and trailing whitespace is ignored.
std::vector<StringView> parse_list(StringView input) {
  using uchar = unsigned char;

  input = strip(input);
  std::vector<StringView> items;
  if (input.empty()) {
    return items;
  }

  const char *const end = input.end();

  const char *current = input.begin();
  const char *begin_delim;
  do {
    const char *begin_item =
        std::find_if(current, end, [](uchar ch) { return !std::isspace(ch); });
    begin_delim = std::find_if(begin_item, end, [](uchar ch) {
      return std::isspace(ch) || ch == ',';
    });

    items.emplace_back(begin_item, std::size_t(begin_delim - begin_item));

    const char *end_delim = std::find_if(
        begin_delim, end, [](uchar ch) { return !std::isspace(ch); });

    if (end_delim != end && *end_delim == ',') {
      ++end_delim;
    }

    current = end_delim;
  } while (begin_delim != end);

  return items;
}

Expected<std::unordered_map<std::string, std::string>> parse_tags(
    std::vector<StringView> list) {
  std::unordered_map<std::string, std::string> tags;

  for (const StringView &token : list) {
    const auto separator = std::find(token.begin(), token.end(), ':');
    if (separator == token.end()) {
      std::string message;
      message += "Unable to parse a key/value from the tag text \"";
      append(message, token);
      // message +=
      //     "\" because it does not contain the separator character \":\".  "
      //     "Error occurred in list of tags \"";
      // append(message, input);
      message += ".";
      return Error{Error::TAG_MISSING_SEPARATOR, std::move(message)};
    }

    std::string key{token.begin(), separator};
    std::string value{separator + 1, token.end()};
    // If there are duplicate values, then the last one wins.
    tags.insert_or_assign(std::move(key), std::move(value));
  }

  return tags;
}

}  // namespace tracing
}  // namespace datadog
