#pragma once

#include <memory>
#include <string>

namespace datadog {
namespace tracing {

class HTTPClient;

struct DatadogAgentConfig {
  std::shared_ptr<HTTPClient> http_client;
  std::string agent_url;
  int flush_interval_milliseconds;
};

}  // namespace tracing
}  // namespace datadog
