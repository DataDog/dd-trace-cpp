#pragma once

#include <string>

namespace datadog::tracing::root_session_id {

inline const std::string& get_or_init(const std::string& runtime_id) {
  static const std::string id = runtime_id;
  return id;
}

}  // namespace datadog::tracing::root_session_id
