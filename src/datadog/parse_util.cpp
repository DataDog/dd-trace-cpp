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
  const auto beg = input.data();
  const auto end = input.data() + input.size();
  const auto status = std::from_chars(beg, end, value, base);
  if (status.ec == std::errc::invalid_argument) {
    std::string message;
    message += "Is not a valid integer: \"";
    append(message, input);
    message += '\"';
    return Error{Error::INVALID_INTEGER, std::move(message)};
  } else if (status.ptr != end) {
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
  if (input.empty()) return input;

  auto begin = input.data();
  auto end = begin + input.size() - 1;

  while (begin && std::isspace(*begin)) ++begin;
  while (end && std::isspace(*end)) --end;

  assert(begin <= end);

  return StringView{begin, std::size_t(end + 1 - begin)};
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

  auto c0 = subject.data();
  auto c1 = prefix.data();
  const auto prefix_end = c1 + prefix.size();
  while (*c0 == *c1) {
    ++c0;
    ++c1;
  }
  
  return c1 == prefix_end;
}

void to_lower(std::string& text) {
  std::transform(text.begin(), text.end(), text.begin(),
                 [](unsigned char ch) { return std::tolower(ch); });
}

}  // namespace tracing
}  // namespace datadog
