#pragma once

// This component provides networking-related miscellanea.

#include <optional>
#include <string>

namespace datadog {
namespace tracing {

std::optional<std::string> get_hostname();

}  // namespace tracing
}  // namespace datadog
