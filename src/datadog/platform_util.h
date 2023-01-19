#pragma once

// This component provides platform-dependent miscellanea.

#include <string>

#include "optional.h"

namespace datadog {
namespace tracing {

Optional<std::string> get_hostname();

}  // namespace tracing
}  // namespace datadog
