#include "collector_response.h"

namespace datadog {
namespace tracing {

std::string CollectorResponse::key(std::string_view service,
                                   std::string_view environment) {
  std::string result;
  result += "service:";
  result += service;
  result += ",env:";
  result += environment;
  return result;
}

const std::string CollectorResponse::key_of_default_rate =
    CollectorResponse::key("", "");

}  // namespace tracing
}  // namespace datadog
