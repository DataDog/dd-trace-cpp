#include "span_matcher.h"

#include <algorithm>

#include "glob.h"
#include "json.hpp"
#include "span_data.h"

namespace datadog {
namespace tracing {
namespace {

bool is_match(std::string_view pattern, std::string_view subject) {
  // Since "*" is the default pattern, optimize for that case.
  return pattern == "*" || glob_match(pattern, subject);
}

}  // namespace

std::string SpanMatcher::to_json() const {
  nlohmann::json object;
  object["service"] = service;
  object["name"] = name;
  object["resource"] = resource;
  object["tags"] = tags;
  return object.dump();
}

bool SpanMatcher::match(const SpanData& span) const {
  return is_match(service, span.service) && is_match(name, span.name) &&
         is_match(resource, span.resource) &&
         std::all_of(tags.begin(), tags.end(), [&](const auto& entry) {
           const auto& [name, pattern] = entry;
           auto found = span.tags.find(name);
           return found != span.tags.end() && is_match(pattern, found->second);
         });
}

}  // namespace tracing
}  // namespace datadog
