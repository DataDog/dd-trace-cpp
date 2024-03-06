#include "string_util.h"

#include <iomanip>
#include <sstream>

namespace datadog {
namespace tracing {

std::string to_string(bool b) { return b ? "true" : "false"; }

std::string to_string(double d, size_t precision) {
  std::stringstream stream;
  stream << std::fixed << std::setprecision(precision) << d;
  return stream.str();
}

std::string join(const std::vector<StringView>& values,
                 const char* const separator) {
  if (values.empty()) return {};
  auto it = values.cbegin();

  std::string res{*it};
  for (++it; it != values.cend(); ++it) {
    res += separator;
    append(res, *it);
  }

  return res;
}

std::string join_propagation_styles(
    const std::vector<PropagationStyle>& values) {
  auto to_string = [](PropagationStyle style) {
    switch (style) {
      case PropagationStyle::B3:
        return "b3";
      case PropagationStyle::DATADOG:
        return "datadog";
      case PropagationStyle::W3C:
        return "tracecontext";
      case PropagationStyle::NONE:
        return "none";
    }
    return "";  ///< unlikely
  };

  if (values.empty()) return "";
  auto it = values.cbegin();

  std::string res{to_string(*it)};
  for (++it; it != values.cend(); ++it) {
    res += ',';
    res += to_string(*it);
  }

  return res;
}

std::string join_tags(
    const std::unordered_map<std::string, std::string>& tagset) {
  if (tagset.empty()) return {};

  auto it = tagset.cbegin();

  std::string res;
  res += it->first;
  res += ":";
  res += it->second;

  for (++it; it != tagset.cend(); ++it) {
    res += ",";
    res += it->first;
    res += ":";
    res += it->second;
  }

  return res;
}

}  // namespace tracing
}  // namespace datadog
