#include "curl.h"

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
#include <thread>
#include <unordered_map>
#include <unordered_set>

#include "dict_reader.h"
#include "dict_writer.h"
#include "http_client.h"
#include "json.hpp"
#include "logger.h"
#include "parse_util.h"
#include "string_view.h"

namespace datadog {
namespace tracing {

using ErrorHandler = HTTPClient::ErrorHandler;
using HeadersSetter = HTTPClient::HeadersSetter;
using ResponseHandler = HTTPClient::ResponseHandler;
using URL = HTTPClient::URL;

class CurlImpl {
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
    void set(StringView key, StringView value) override;
  };

  class HeaderReader : public DictReader {
    std::unordered_map<std::string, std::string> *response_headers_lower_;
    mutable std::string buffer_;

   public:
    explicit HeaderReader(
        std::unordered_map<std::string, std::string> *response_headers_lower);
    std::optional<StringView> lookup(StringView key) const override;
    void visit(const std::function<void(StringView key, StringView value)>
                   &visitor) const override;
  };

  void run();
  void handle_message(const CURLMsg &);
  CURLcode log_on_error(CURLcode result);
  CURLMcode log_on_error(CURLMcode result);

  static std::size_t on_read_header(char *data, std::size_t, std::size_t length,
                                    void *user_data);
  static std::size_t on_read_body(char *data, std::size_t, std::size_t length,
                                  void *user_data);
  static bool is_non_whitespace(unsigned char);
  static char to_lower(unsigned char);
  static StringView trim(StringView);

 public:
  explicit CurlImpl(const std::shared_ptr<Logger> &logger);
  ~CurlImpl();

  Expected<void> post(const URL &url, HeadersSetter set_headers,
                      std::string body, ResponseHandler on_response,
                      ErrorHandler on_error);

  void drain(std::chrono::steady_clock::time_point deadline);
};

namespace {

void throw_on_error(CURLcode result) {
  if (result != CURLE_OK) {
    throw result;
  }
}

}  // namespace

Curl::Curl(const std::shared_ptr<Logger> &logger)
    : impl_(new CurlImpl{logger}) {}

Curl::~Curl() { delete impl_; }

Expected<void> Curl::post(const URL &url, HeadersSetter set_headers,
                          std::string body, ResponseHandler on_response,
                          ErrorHandler on_error) {
  return impl_->post(url, set_headers, body, on_response, on_error);
}

void Curl::drain(std::chrono::steady_clock::time_point deadline) {
  impl_->drain(deadline);
}

nlohmann::json Curl::config_json() const {
  return nlohmann::json::object({{"type", "datadog::tracing::Curl"}});
}

CurlImpl::CurlImpl(const std::shared_ptr<Logger> &logger)
    : logger_(logger), shutting_down_(false), num_active_handles_(0) {
  curl_global_init(CURL_GLOBAL_ALL);
  multi_handle_ = curl_multi_init();
  if (multi_handle_ == nullptr) {
    logger_->log_error(Error{
        Error::CURL_HTTP_CLIENT_SETUP_FAILED,
        "Unable to initialize a curl multi-handle for sending requests."});
    return;
  }

  try {
    event_loop_ = std::thread([this]() { run(); });
  } catch (const std::system_error &error) {
    logger_->log_error(
        Error{Error::CURL_HTTP_CLIENT_SETUP_FAILED, error.what()});

    // Usually the worker thread would do this, but since the thread failed to
    // start, do it here.
    (void)curl_multi_cleanup(multi_handle_);
    curl_global_cleanup();

    // Mark this object as not working.
    multi_handle_ = nullptr;
  }
}

CurlImpl::~CurlImpl() {
  if (multi_handle_ == nullptr) {
    // We're not running; nothing to shut down.
    return;
  }

  {
    std::lock_guard<std::mutex> lock(mutex_);
    shutting_down_ = true;
  }
  log_on_error(curl_multi_wakeup(multi_handle_));
  event_loop_.join();
}

Expected<void> CurlImpl::post(const HTTPClient::URL &url,
                              HeadersSetter set_headers, std::string body,
                              ResponseHandler on_response,
                              ErrorHandler on_error) try {
  if (multi_handle_ == nullptr) {
    return Error{Error::CURL_HTTP_CLIENT_NOT_RUNNING,
                 "Unable to send request via libcurl because the HTTP client "
                 "failed to start."};
  }

  auto request = std::make_unique<Request>();

  request->request_body = std::move(body);
  request->on_response = std::move(on_response);
  request->on_error = std::move(on_error);

  std::unique_ptr<CURL, decltype(&curl_easy_cleanup)> handle{
      curl_easy_init(), &curl_easy_cleanup};

  if (!handle) {
    return Error{Error::CURL_REQUEST_SETUP_FAILED,
                 "unable to initialize a curl handle for request sending"};
  }

  // throw_on_error(curl_easy_setopt(handle.get(), CURLOPT_VERBOSE, 1));

  throw_on_error(
      curl_easy_setopt(handle.get(), CURLOPT_PRIVATE, request.get()));
  throw_on_error(curl_easy_setopt(handle.get(), CURLOPT_ERRORBUFFER,
                                  request->error_buffer));
  throw_on_error(curl_easy_setopt(handle.get(), CURLOPT_POST, 1));
  throw_on_error(curl_easy_setopt(handle.get(), CURLOPT_POSTFIELDSIZE,
                                  request->request_body.size()));
  throw_on_error(curl_easy_setopt(handle.get(), CURLOPT_POSTFIELDS,
                                  request->request_body.data()));
  throw_on_error(
      curl_easy_setopt(handle.get(), CURLOPT_HEADERFUNCTION, &on_read_header));
  throw_on_error(
      curl_easy_setopt(handle.get(), CURLOPT_HEADERDATA, request.get()));
  throw_on_error(
      curl_easy_setopt(handle.get(), CURLOPT_WRITEFUNCTION, &on_read_body));
  throw_on_error(
      curl_easy_setopt(handle.get(), CURLOPT_WRITEDATA, request.get()));
  if (url.scheme == "unix" || url.scheme == "http+unix" ||
      url.scheme == "https+unix") {
    throw_on_error(curl_easy_setopt(handle.get(), CURLOPT_UNIX_SOCKET_PATH,
                                    url.authority.c_str()));
    // The authority section of the URL is ignored when a unix domain socket is
    // to be used.
    throw_on_error(curl_easy_setopt(handle.get(), CURLOPT_URL,
                                    ("http://localhost" + url.path).c_str()));
  } else {
    throw_on_error(curl_easy_setopt(
        handle.get(), CURLOPT_URL,
        (url.scheme + "://" + url.authority + url.path).c_str()));
  }

  HeaderWriter writer;
  set_headers(writer);
  std::unique_ptr<curl_slist, decltype(&curl_slist_free_all)> headers{
      writer.release(), &curl_slist_free_all};
  request->request_headers = headers.get();
  throw_on_error(curl_easy_setopt(handle.get(), CURLOPT_HTTPHEADER,
                                  request->request_headers));

  std::list<CURL *> node;
  node.push_back(handle.get());
  {
    std::lock_guard<std::mutex> lock(mutex_);
    new_handles_.splice(new_handles_.end(), node);

    headers.release();
    handle.release();
    request.release();
  }
  log_on_error(curl_multi_wakeup(multi_handle_));

  return std::nullopt;
} catch (CURLcode error) {
  return Error{Error::CURL_REQUEST_SETUP_FAILED, curl_easy_strerror(error)};
}

void CurlImpl::drain(std::chrono::steady_clock::time_point deadline) {
  std::unique_lock<std::mutex> lock(mutex_);
  no_requests_.wait_until(lock, deadline, [this]() {
    return num_active_handles_ == 0 && new_handles_.empty();
  });
}

std::size_t CurlImpl::on_read_header(char *data, std::size_t,
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

  const auto key = strip(range(begin, colon));
  const auto value = strip(range(colon + 1, end));

  std::string key_lower;
  key_lower.reserve(key.size());
  std::transform(key.begin(), key.end(), std::back_inserter(key_lower),
                 &to_lower);

  request->response_headers_lower.emplace(std::move(key_lower), value);
  return length;
}

bool CurlImpl::is_non_whitespace(unsigned char ch) { return !std::isspace(ch); }

char CurlImpl::to_lower(unsigned char ch) { return std::tolower(ch); }

std::size_t CurlImpl::on_read_body(char *data, std::size_t, std::size_t length,
                                   void *user_data) {
  const auto request = static_cast<Request *>(user_data);
  request->response_body.append(data, length);
  return length;
}

CURLcode CurlImpl::log_on_error(CURLcode result) {
  if (result != CURLE_OK) {
    logger_->log_error(
        Error{Error::CURL_HTTP_CLIENT_ERROR, curl_easy_strerror(result)});
  }
  return result;
}

CURLMcode CurlImpl::log_on_error(CURLMcode result) {
  if (result != CURLM_OK) {
    logger_->log_error(
        Error{Error::CURL_HTTP_CLIENT_ERROR, curl_multi_strerror(result)});
  }
  return result;
}

void CurlImpl::run() {
  int num_messages_remaining;
  CURLMsg *message;
  std::unique_lock<std::mutex> lock(mutex_);

  for (;;) {
    log_on_error(curl_multi_perform(multi_handle_, &num_active_handles_));
    if (num_active_handles_ == 0) {
      no_requests_.notify_all();
    }

    // If a request is done or errored out, curl will enqueue a "message" for
    // us to handle.  Handle any pending messages.
    while ((message =
                curl_multi_info_read(multi_handle_, &num_messages_remaining))) {
      handle_message(*message);
    }

    const int max_wait_milliseconds = 10 * 1000;
    lock.unlock();
    log_on_error(curl_multi_poll(multi_handle_, nullptr, 0,
                                 max_wait_milliseconds, nullptr));
    lock.lock();

    // New requests might have been added while we were sleeping.
    for (; !new_handles_.empty(); new_handles_.pop_front()) {
      CURL *const handle = new_handles_.front();
      log_on_error(curl_multi_add_handle(multi_handle_, handle));
      request_handles_.insert(handle);
    }

    if (shutting_down_) {
      break;
    }
  }

  // We're shutting down.  Clean up any remaining request handles.
  for (const auto &handle : request_handles_) {
    char *user_data;
    if (log_on_error(curl_easy_getinfo(handle, CURLINFO_PRIVATE, &user_data)) ==
        CURLE_OK) {
      delete reinterpret_cast<Request *>(user_data);
    }

    log_on_error(curl_multi_remove_handle(multi_handle_, handle));
  }

  request_handles_.clear();
  log_on_error(curl_multi_cleanup(multi_handle_));
  curl_global_cleanup();
}

void CurlImpl::handle_message(const CURLMsg &message) {
  if (message.msg != CURLMSG_DONE) {
    return;
  }

  auto *const request_handle = message.easy_handle;
  char *user_data;
  if (log_on_error(curl_easy_getinfo(request_handle, CURLINFO_PRIVATE,
                                     &user_data)) != CURLE_OK) {
    return;
  }
  auto &request = *reinterpret_cast<Request *>(user_data);

  // `request` is done.  If we got a response, then call the response
  // handler.  If an error occurred, then call the error handler.
  const auto result = message.data.result;
  if (result != CURLE_OK) {
    std::string error_message;
    error_message += "Error sending request with libcurl (";
    error_message += curl_easy_strerror(result);
    error_message += "): ";
    error_message += request.error_buffer;
    request.on_error(
        Error{Error::CURL_REQUEST_FAILURE, std::move(error_message)});
  } else {
    long status;
    if (log_on_error(curl_easy_getinfo(request_handle, CURLINFO_RESPONSE_CODE,
                                       &status)) != CURLE_OK) {
      status = -1;
    }
    HeaderReader reader(&request.response_headers_lower);
    request.on_response(static_cast<int>(status), reader,
                        std::move(request.response_body));
  }

  log_on_error(curl_multi_remove_handle(multi_handle_, request_handle));
  curl_easy_cleanup(request_handle);
  request_handles_.erase(request_handle);
  delete &request;
}

CurlImpl::Request::~Request() { curl_slist_free_all(request_headers); }

CurlImpl::HeaderWriter::~HeaderWriter() { curl_slist_free_all(list_); }

curl_slist *CurlImpl::HeaderWriter::release() {
  auto list = list_;
  list_ = nullptr;
  return list;
}

void CurlImpl::HeaderWriter::set(StringView key, StringView value) {
  buffer_.clear();
  buffer_ += key;
  buffer_ += ": ";
  buffer_ += value;

  list_ = curl_slist_append(list_, buffer_.c_str());
}

CurlImpl::HeaderReader::HeaderReader(
    std::unordered_map<std::string, std::string> *response_headers_lower)
    : response_headers_lower_(response_headers_lower) {}

std::optional<StringView> CurlImpl::HeaderReader::lookup(StringView key) const {
  buffer_.clear();
  std::transform(key.begin(), key.end(), std::back_inserter(buffer_),
                 &to_lower);

  const auto found = response_headers_lower_->find(buffer_);
  if (found == response_headers_lower_->end()) {
    return std::nullopt;
  }
  return found->second;
}

void CurlImpl::HeaderReader::visit(
    const std::function<void(StringView key, StringView value)> &visitor)
    const {
  for (const auto &[key, value] : *response_headers_lower_) {
    visitor(key, value);
  }
}

}  // namespace tracing
}  // namespace datadog
