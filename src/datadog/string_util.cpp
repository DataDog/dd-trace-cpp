#include "string_util.h"

#include <iomanip>
#include <sstream>

namespace datadog {
namespace tracing {
namespace {

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

StringView trim(StringView str) {
  str.remove_prefix(std::min(str.find_first_not_of(' '), str.size()));
  const auto pos = str.find_last_not_of(' ');
  if (pos != str.npos) str.remove_suffix(str.size() - pos - 1);
  return str;
}

}  // namespace tracing
}  // namespace datadog
