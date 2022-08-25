#pragma once

#include <string>

namespace datadog {
namespace tracing {
namespace tags {

extern const std::string environment;
extern const std::string service_name;
extern const std::string span_type;
extern const std::string operation_name;
extern const std::string resource_name;
extern const std::string analytics_event;
extern const std::string manual_keep;
extern const std::string manual_drop;
extern const std::string version;

namespace internal {
extern const std::string propagation_error;
extern const std::string decision_maker;
}  // namespace internal

}  // namespace tags
}  // namespace tracing
}  // namespace datadog
