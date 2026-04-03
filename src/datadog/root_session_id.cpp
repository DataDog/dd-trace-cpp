#include "root_session_id.h"

#include <mutex>

namespace datadog {
namespace tracing {
namespace root_session_id {

namespace {

std::string& instance() {
  static std::string id;
  return id;
}

std::once_flag& flag() {
  static std::once_flag f;
  return f;
}

}  // namespace

void set(const std::string& id) {
  std::call_once(flag(), [&]() { instance() = id; });
}

const std::string& get() { return instance(); }

}  // namespace root_session_id
}  // namespace tracing
}  // namespace datadog
