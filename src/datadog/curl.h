#pragma once

#include <chrono>
#include <memory>
#include <string>

#include "http_client.h"

namespace datadog {
namespace tracing {

class CurlImpl;
class Logger;

class Curl : public HTTPClient {
  CurlImpl* impl_;

 public:
  explicit Curl(const std::shared_ptr<Logger>& logger);
  ~Curl();

  Curl(const Curl&) = delete;

  Expected<void> post(const URL& url, HeadersSetter set_headers,
                      std::string body, ResponseHandler on_response,
                      ErrorHandler on_error) override;

  void drain(std::chrono::steady_clock::time_point deadline) override;
};

}  // namespace tracing
}  // namespace datadog
