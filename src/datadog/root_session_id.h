#pragma once

#include <string>

namespace datadog {
namespace tracing {
namespace root_session_id {

const std::string& get_or_init(const std::string& runtime_id);

}  // namespace root_session_id
}  // namespace tracing
}  // namespace datadog
