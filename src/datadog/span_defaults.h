#pragma once

#include <string>
#include <unordered_map>

namespace datadog {
namespace tracing {

struct SpanDefaults {
  std::string service;
  std::string service_type = "web";
  std::string environment = "";
  std::string version = "";
  std::string name = "";
  std::unordered_map<std::string, std::string> tags;
};

}  // namespace tracing
}  // namespace datadog
