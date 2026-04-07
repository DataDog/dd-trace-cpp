#pragma once

#include <string>

namespace datadog {
namespace tracing {
namespace root_session_id {

// Meyer's singleton for the root session ID. The first call initializes the
// value; subsequent calls return the same value regardless of the argument.
// Thread-safe via atomic CAS. Immutable after init, so fork-safe.
const std::string& get_or_init(const std::string& runtime_id);

}  // namespace root_session_id
}  // namespace tracing
}  // namespace datadog
