#include "null_collector.h"

#include "json.hpp"

namespace datadog {
namespace tracing {

void NullCollector::config_json(nlohmann::json& destination) const {
  // clang-format off
    destination = nlohmann::json::object({
        {"type", "NullCollector"},
        {"config", nlohmann::json::object({})},
    });
  // clang-format on
}

}  // namespace tracing
}  // namespace datadog
