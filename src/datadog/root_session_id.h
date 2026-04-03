#pragma once

#include <mutex>
#include <string>

namespace datadog {
namespace tracing {

// Thread-safe singleton for the root session ID. The first call to `set`
// captures the value; subsequent calls are no-ops. Immutable after init,
// so fork-safe.

namespace root_session_id {

inline std::string& instance() {
  static std::string id;
  return id;
}

inline std::once_flag& flag() {
  static std::once_flag f;
  return f;
}

inline void set(const std::string& id) {
  std::call_once(flag(), [&]() { instance() = id; });
}

inline const std::string& get() { return instance(); }

}  // namespace root_session_id

}  // namespace tracing
}  // namespace datadog
