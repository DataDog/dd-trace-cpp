#pragma once

#include <curl/curl.h>

#include <cstddef>
#include <mutex>
#include <sstream>
#include <thread>
#include <unordered_map>
#include <unordered_set>

#include "http_client.h"

namespace datadog {
namespace tracing {

class Curl : public HTTPClient {
  std::mutex mutex_;
  CURLM *multi_handle_;
  std::unordered_set<CURL *> request_handles_;
  bool shutting_down_;
  std::thread event_loop_;

  struct Request {
    std::string request_body;
    ResponseHandler on_response;
    ErrorHandler on_error;
    char error_buffer[CURL_ERROR_SIZE];
    int response_status;
    std::unordered_map<std::string, std::string> response_headers_lower;
    std::stringstream response_body;
  };

  void run();
  static std::size_t on_read_header(char *data, std::size_t, std::size_t length, void *user_data);
  static std::size_t on_read_body(char *data, std::size_t, std::size_t length, void *user_data);

 public:
  Curl();
  ~Curl();

  virtual std::optional<Error> post(std::string_view URL,
                                    HeadersSetter set_headers, std::string body,
                                    ResponseHandler on_response,
                                    ErrorHandler on_error) override;
};

inline Curl::Curl()
    : multi_handle_((curl_global_init(CURL_GLOBAL_ALL), curl_multi_init())),
      shutting_down_(false),
      event_loop_([this]() { run(); }) {}

inline Curl::~Curl() {
  {
    std::lock_guard<std::mutex> lock(mutex_);
    shutting_down_ = true;
    curl_multi_wakeup(multi_handle_);
  }

  event_loop_.join();
}

inline std::optional<Error> Curl::post(std::string_view URL,
                                       HeadersSetter set_headers,
                                       std::string body,
                                       ResponseHandler on_response,
                                       ErrorHandler on_error) {
  auto request = std::make_unique<Request>();

  request->request_body = std::move(body);
  request->on_response = std::move(on_response);
  request->on_error = std::move(on_error);
  
  CURL *handle = curl_easy_init();
  // TODO: error handling
  curl_easy_setopt(handle, CURLOPT_ERRORBUFFER, request->error_buffer);
  curl_easy_setopt(handle, CURLOPT_POST, 1);
  curl_easy_setopt(handle, CURLOPT_POSTFIELDSIZE, request->request_body.size());
  curl_easy_setopt(handle, CURLOPT_POSTFIELDS, request->request_body.data());
  curl_easy_setopt(handle, CURLOPT_HEADERFUNCTION, &on_read_header);
  curl_easy_setopt(handle, CURLOPT_HEADERDATA, request.get());
  curl_easy_setopt(handle, CURLOPT_WRITEFUNCTION, &on_read_body);
  curl_easy_setopt(handle, CURLOPT_WRITEDATA, request.get());
  // TODO
  
  return std::nullopt;  // TODO
}

inline std::size_t Curl::on_read_header(char *data, std::size_t, std::size_t length, void *user_data) {
    const auto request = static_cast<Request*>(user_data);
    // TODO:
    //    "    Foo-Bar:   thingy, thingy, thing   "
    //    -> {"foo-bar", "thingy, thingy, thing"}
    //
    // Positions:
    // A. First non-whitespace
    // B. First colon
    // C. Last non-whitespace
    // D. First non-whitespace after colon
    //
    // Corner case:
    // - C = B,  D = end
    //     - The header value is empty (or all whitespace).
    //
    return length;
}

inline std::size_t Curl::on_read_body(char *data, std::size_t, std::size_t length, void *user_data) {
    const auto request = static_cast<Request*>(user_data);
    if (!request->response_body.write(data, length)) {
        return -1;  // Any value other than `length` will do.
    }
    return length;
}

inline void Curl::run() {
  // TODO
}

}  // namespace tracing
}  // namespace datadog
