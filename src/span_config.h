#pragma once

#include <optional>
#include <string>
#include <unordered_map>
#include <variant>

#include "clock.h"
#include "validated.h"

namespace datadog {
namespace tracing {

struct SpanConfig {
  std::optional<std::string> service;
  std::optional<std::string> service_type;
  std::optional<std::string> name;
  std::optional<std::string> resource;
  std::optional<TimePoint> start;
  std::unordered_map<std::string, std::string> tags;
};

std::variant<Validated<SpanConfig>, Error> validate_config(
    const SpanConfig& config);

}  // namespace tracing
}  // namespace datadog
