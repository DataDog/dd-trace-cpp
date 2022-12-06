#pragma once

// This component provides networking-related miscellanea.

#include "optional.h"
#include <string>

namespace datadog {
namespace tracing {

Optional<std::string> get_hostname();

}  // namespace tracing
}  // namespace datadog
