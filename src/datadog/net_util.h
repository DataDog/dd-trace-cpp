#pragma once

#include <optional>
#include <string>

namespace datadog {
namespace tracing {

std::optional<std::string> get_hostname();

}  // namespace tracing
}  // namespace datadog
