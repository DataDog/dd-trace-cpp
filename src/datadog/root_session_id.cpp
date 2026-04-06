#include "root_session_id.h"

#include <mutex>

namespace datadog {
namespace tracing {
namespace root_session_id {

namespace {

std::mutex& mutex() {
  static std::mutex m;
  return m;
}

std::string& instance() {
  static std::string id;
  return id;
}

bool& initialized() {
  static bool init = false;
  return init;
}

}  // namespace

void set(const std::string& id) {
  std::lock_guard<std::mutex> lock(mutex());
  if (!initialized()) {
    instance() = id;
    initialized() = true;
  }
}

const std::string& get() { return instance(); }

}  // namespace root_session_id
}  // namespace tracing
}  // namespace datadog
