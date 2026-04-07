#include "root_session_id.h"

#include <atomic>

namespace datadog {
namespace tracing {
namespace root_session_id {

namespace {

std::string& instance() {
  static std::string id;
  return id;
}

std::atomic<bool>& initialized() {
  static std::atomic<bool> flag{false};
  return flag;
}

}  // namespace

const std::string& get_or_init(const std::string& runtime_id) {
  bool expected = false;
  if (initialized().compare_exchange_strong(expected, true)) {
    instance() = runtime_id;
  }
  return instance();
}

}  // namespace root_session_id
}  // namespace tracing
}  // namespace datadog
