#include "root_session_id.h"

namespace datadog {
namespace tracing {
namespace root_session_id {

namespace {

std::string& instance() {
  static std::string id;
  return id;
}

}  // namespace

void set(const std::string& id) {
  if (instance().empty()) {
    instance() = id;
  }
}

const std::string& get() { return instance(); }

}  // namespace root_session_id
}  // namespace tracing
}  // namespace datadog
