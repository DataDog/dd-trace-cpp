#pragma once

#include <string>

namespace datadog {
namespace tracing {
namespace root_session_id {

// Thread-safe singleton for the root session ID. The first call to `set`
// captures the value; subsequent calls are no-ops. Immutable after init,
// so fork-safe.
void set(const std::string& id);
const std::string& get();

}  // namespace root_session_id
}  // namespace tracing
}  // namespace datadog
