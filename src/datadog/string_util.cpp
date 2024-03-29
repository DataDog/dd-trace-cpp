#include "string_util.h"

#include <algorithm>
#include <iomanip>
#include <sstream>

namespace datadog {
namespace tracing {
namespace {

constexpr StringView k_spaces_characters = " \f\n\r\t\v";

template <typename Sequence, typename Func>
std::string join(const Sequence& elements, StringView separator,
                 Func&& append_element) {
  auto iter = std::begin(elements);
  const auto end = std::end(elements);
  std::string result;
  if (iter == end) {
    return result;
  }
  append_element(result, *iter);
  for (++iter; iter != end; ++iter) {
    append(result, separator);
    append_element(result, *iter);
  }
  return result;
}

}  // namespace

void to_lower(std::string& text) {
  std::transform(text.begin(), text.end(), text.begin(),
                 [](unsigned char ch) { return std::tolower(ch); });
}

std::string to_string(bool b) { return b ? "true" : "false"; }

std::string to_string(double d, size_t precision) {
  std::stringstream stream;
  stream << std::fixed << std::setprecision(precision) << d;
  return stream.str();
}

std::string join(const std::vector<StringView>& values, StringView separator) {
  return join(values, separator, [](std::string& result, StringView value) {
    append(result, value);
  });
}

std::string join_propagation_styles(
    const std::vector<PropagationStyle>& values) {
  return join(values, ",", [](std::string& result, PropagationStyle style) {
    switch (style) {
      case PropagationStyle::B3:
        result += "b3";
        break;
      case PropagationStyle::DATADOG:
        result += "datadog";
        break;
      case PropagationStyle::W3C:
        result += "tracecontext";
        break;
      case PropagationStyle::NONE:
        result += "none";
        break;
    }
  });
}

std::string join_tags(
    const std::unordered_map<std::string, std::string>& values) {
  return join(values, ",", [](std::string& result, const auto& entry) {
    const auto& [key, value] = entry;
    result += key;
    result += ':';
    result += value;
  });
}

bool starts_with(StringView subject, StringView prefix) {
  if (prefix.size() > subject.size()) {
    return false;
  }

  return std::mismatch(subject.begin(), subject.end(), prefix.begin()).second ==
         prefix.end();
}

StringView trim(StringView str) {
  str.remove_prefix(
      std::min(str.find_first_not_of(k_spaces_characters), str.size()));
  const auto pos = str.find_last_not_of(k_spaces_characters);
  if (pos != str.npos) str.remove_suffix(str.size() - pos - 1);
  return str;
}

}  // namespace tracing
}  // namespace datadog
