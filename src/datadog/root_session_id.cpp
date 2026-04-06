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

std::mutex& mutex() {
  static std::mutex m;
  return m;
}

}  // namespace

void set(const std::string& id) {
  std::lock_guard<std::mutex> lock(mutex());
  if (instance().empty()) {
    instance() = id;
  }
}

const std::string& get() { return instance(); }

}  // namespace root_session_id
}  // namespace tracing
}  // namespace datadog
