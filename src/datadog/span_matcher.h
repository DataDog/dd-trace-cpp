#pragma once

#include <string>
#include <unordered_map>

namespace datadog {
namespace tracing {

struct SpanData;

struct SpanMatcher {
  std::string service = "*";
  std::string name = "*";
  std::string resource = "*";
  std::unordered_map<std::string, std::string> tags;

  bool match(const SpanData&) const;
  std::string to_json() const;
};

}  // namespace tracing
}  // namespace datadog
