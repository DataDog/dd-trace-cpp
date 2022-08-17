#pragma once

#include <optional>
#include <string>
#include <unordered_map>

#include "clock.h"

namespace datadog {
namespace tracing {

struct SpanConfig {
  std::optional<std::string> service;
  std::optional<std::string> name;
  std::optional<std::string> resource;
  std::optional<std::string> type;
  std::optional<TimePoint> start;
  std::unordered_map<std::string, std::string> tags;
};

}  // namespace tracing
}  // namespace datadog
