#pragma once

// This component provides platform-dependent miscellanea.

#include <string>

#include "optional.h"

namespace datadog {
namespace tracing {

Optional<std::string> get_hostname();

int get_process_id();

int at_fork_in_child(void (*on_fork)());

}  // namespace tracing
}  // namespace datadog
