#pragma once

#include <curl/curl.h>

#include <algorithm>
#include <cctype>
#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <iterator>
#include <list>
#include <memory>
#include <mutex>
#include <string_view>
#include <thread>
#include <unordered_map>
#include <unordered_set>

#include "dict_reader.h"
#include "dict_writer.h"
#include "http_client.h"
#include "logger.h"

namespace datadog {
namespace tracing {

class Curl : public HTTPClient {
  std::mutex mutex_;
  std::shared_ptr<Logger> logger_;
  CURLM *multi_handle_;
  std::unordered_set<CURL *> request_handles_;
  std::list<CURL *> new_handles_;
  bool shutting_down_;
  int num_active_handles_;
  std::condition_variable no_requests_;
  std::thread event_loop_;

  struct Request {
    curl_slist *request_headers = nullptr;
    std::string request_body;
    ResponseHandler on_response;
    ErrorHandler on_error;
    char error_buffer[CURL_ERROR_SIZE] = "";
    std::unordered_map<std::string, std::string> response_headers_lower;
    std::string response_body;

    ~Request();
  };

  class HeaderWriter : public DictWriter {
    curl_slist *list_ = nullptr;
    std::string buffer_;

   public:
    ~HeaderWriter();
    curl_slist *release();
    virtual void set(std::string_view key, std::string_view value) override;
  };

  class HeaderReader : public DictReader {
    std::unordered_map<std::string, std::string> *response_headers_lower_;
    mutable std::string buffer_;

   public:
    explicit HeaderReader(
        std::unordered_map<std::string, std::string> *response_headers_lower);
    virtual std::optional<std::string_view> lookup(
        std::string_view key) const override;
    virtual void visit(
        const std::function<void(std::string_view key, std::string_view value)>
            &visitor) const override;
  };

  void run();
  CURLcode log_on_error(CURLcode result);
  CURLMcode log_on_error(CURLMcode result);

  static std::size_t on_read_header(char *data, std::size_t, std::size_t length,
                                    void *user_data);
  static std::size_t on_read_body(char *data, std::size_t, std::size_t length,
                                  void *user_data);
  static bool is_non_whitespace(unsigned char);
  static char to_lower(unsigned char);
  static std::string_view trim(std::string_view);

 public:
  explicit Curl(const std::shared_ptr<Logger> &logger);
  ~Curl();

  virtual Expected<void> post(const URL &url, HeadersSetter set_headers,
                              std::string body, ResponseHandler on_response,
                              ErrorHandler on_error) override;

  virtual void drain(std::chrono::steady_clock::time_point deadline) override;
};

}  // namespace tracing
}  // namespace datadog
