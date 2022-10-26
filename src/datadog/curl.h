#pragma once

// This component provides a `class`, `Curl`, that implements the `HTTPClient`
// interface in terms of [libcurl][1].  `class Curl` manages a thread that is
// used as the event loop for libcurl.
//
// If this library was built in a mode that does not include libcurl, then this
// file and its implementation, `curl.cpp`, will not be included.
//
// [1]: https://curl.se/libcurl/

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
