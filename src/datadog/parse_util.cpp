#include "parse_util.h"

#include <algorithm>
#include <cctype>
#include <string>

#include "error.h"

namespace datadog {
namespace tracing {
namespace {

template <typename Integer>
Expected<Integer> parse_integer(std::string_view input, int base,
                                std::string_view kind) {
  Integer value;
  input = strip(input);
  const auto status = std::from_chars(input.begin(), input.end(), value, base);
  if (status.ec == std::errc::invalid_argument) {
    std::string message;
    message += "Is not a valid integer: \"";
    message += input;
    message += '\"';
    return Error{Error::INVALID_INTEGER, std::move(message)};
  } else if (status.ptr != input.end()) {
    std::string message;
    message += "Integer has trailing characters in: \"";
    message += input;
    message += '\"';
    return Error{Error::INVALID_INTEGER, std::move(message)};
  } else if (status.ec == std::errc::result_out_of_range) {
    std::string message;
    message += "Integer is not within the range of ";
    message += kind;
    message += ": ";
    message += input;
    return Error{Error::OUT_OF_RANGE_INTEGER, std::move(message)};
  }
  return value;
}

}  // namespace

std::string_view strip(std::string_view input) {
  const auto not_whitespace = [](unsigned char ch) {
    return !std::isspace(ch);
  };
  const char* const begin =
      std::find_if(input.begin(), input.end(), not_whitespace);
  const char* const end =
      std::find_if(input.rbegin(), input.rend(), not_whitespace).base();

  if (end < begin) {
    return std::string_view{};
  }
  return std::string_view{begin, std::size_t(end - begin)};
}

Expected<std::uint64_t> parse_uint64(std::string_view input, int base) {
  return parse_integer<std::uint64_t>(input, base, "64-bit unsigned");
}

Expected<int> parse_int(std::string_view input, int base) {
  return parse_integer<int>(input, base, "int");
}

Expected<double> parse_double(std::string_view input,
                              std::chars_format format) {
  // This is very similar to `parse_integer`, above.
  double value;
  input = strip(input);
  const auto status =
      std::from_chars(input.begin(), input.end(), value, format);

  if (status.ec == std::errc::invalid_argument) {
    std::string message;
    message += "Is not a valid number: \"";
    message += input;
    message += '\"';
    return Error{Error::INVALID_DOUBLE, std::move(message)};
  } else if (status.ptr != input.end()) {
    std::string message;
    message += "Number has trailing characters in: \"";
    message += input;
    message += '\"';
    return Error{Error::INVALID_DOUBLE, std::move(message)};
  } else if (status.ec == std::errc::result_out_of_range) {
    std::string message;
    message +=
        "Number is not within the range of double precision floating point:";
    message += input;
    return Error{Error::OUT_OF_RANGE_DOUBLE, std::move(message)};
  }
  return value;
}

}  // namespace tracing
}  // namespace datadog
