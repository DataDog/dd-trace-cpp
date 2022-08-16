#pragma once

#include <curl/curl.h>

#include <algorithm>
#include <cctype>
#include <cstddef>
#include <iterator>
#include <mutex>
#include <sstream>
#include <string_view>
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
  static std::size_t on_read_header(char *data, std::size_t, std::size_t length,
                                    void *user_data);
  static std::size_t on_read_body(char *data, std::size_t, std::size_t length,
                                  void *user_data);
  static bool is_non_whitespace(unsigned char);
  static char to_lower(unsigned char);
  static std::string_view trim(std::string_view);

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
  (void)URL;
  (void)set_headers;

  return std::nullopt;  // TODO
}

inline std::size_t Curl::on_read_header(char *data, std::size_t,
                                        std::size_t length, void *user_data) {
  const auto request = static_cast<Request *>(user_data);
  // The idea is:
  //
  //         "    Foo-Bar  :   thingy, thingy, thing   \r\n"
  //    -> {"foo-bar", "thingy, thingy, thing"}
  //
  // There isn't always a colon.  Inputs without a colon can be ignored:
  //
  // > For an HTTP transfer, the status line and the blank line preceding the
  // > response body are both included as headers and passed to this
  // > function.
  //
  // https://curl.se/libcurl/c/CURLOPT_HEADERFUNCTION.html
  //

  const char *const begin = data;
  const char *const end = begin + length;
  const char *const colon = std::find(begin, end, ':');
  if (colon == end) {
    return length;
  }

  const auto key = trim(std::string_view(begin, colon - begin));
  const auto value = trim(std::string_view(colon + 1, end - (colon + 1)));

  std::string key_lower;
  key_lower.reserve(key.size());
  std::transform(key.begin(), key.end(), std::back_inserter(key_lower),
                 &to_lower);

  request->response_headers_lower.emplace(std::move(key_lower), value);
  return length;
}

inline bool Curl::is_non_whitespace(unsigned char ch) {
  return !std::isspace(ch);
}

inline char Curl::to_lower(unsigned char ch) { return std::tolower(ch); }

inline std::string_view Curl::trim(std::string_view source) {
  const auto first_non_whitespace =
      std::find_if(source.begin(), source.end(), &is_non_whitespace);
  const auto after_last_non_whitespace =
      std::find_if(source.rbegin(), source.rend(), &is_non_whitespace).base();
  const auto trimmed_length = after_last_non_whitespace - first_non_whitespace;
  return std::string_view(first_non_whitespace, trimmed_length);
}

inline std::size_t Curl::on_read_body(char *data, std::size_t,
                                      std::size_t length, void *user_data) {
  const auto request = static_cast<Request *>(user_data);
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
