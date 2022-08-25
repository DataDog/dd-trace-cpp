#pragma once

#include <string>
#include <string_view>
#include <unordered_map>

#include "rate.h"

namespace datadog {
namespace tracing {

struct CollectorResponse {
  static std::string key(std::string_view service,
                         std::string_view environment);
  static const std::string key_of_default_rate;
  std::unordered_map<std::string, Rate> sample_rate_by_key;
};

}  // namespace tracing
}  // namespace datadog
