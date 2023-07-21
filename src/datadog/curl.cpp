#include "curl.h"

#include <algorithm>
#include <cassert>
#include <cctype>
#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <iterator>
#include <memory>
#include <mutex>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

#include "dict_reader.h"
#include "dict_writer.h"
#include "http_client.h"
#include "json.hpp"
#include "logger.h"
#include "parse_util.h"
#include "string_view.h"

namespace datadog {
namespace tracing {
namespace {

// `libcurl` is the default implementation: it calls `curl_*` functions under
// the hood. There's nothing special about this instance, it's just an
// instance of the default implementation that we can refer to in this file.
CurlLibrary libcurl;

void *get_user_data(CurlLibrary &curl, CURL *handle) {
  // libcurl uses a `char*` for legacy reasons. It's really a `void*`.
  char *user_data = nullptr;
  // As of libcurl 7.10.3, released January 14 2003, getting the private data
  // pointer always succeeds.
  (void)curl.easy_getinfo_private(handle, &user_data);
  return user_data;
}

void set_user_data(CurlLibrary &curl, CURL *handle, void *user_data) {
  // As of libcurl 7.10.3, released January 14 2003, setting the private data
  // pointer always succeeds.
  (void)curl.easy_setopt_private(handle, user_data);
}

}  // namespace

CURL *CurlLibrary::easy_init() { return curl_easy_init(); }

void CurlLibrary::easy_cleanup(CURL *handle) { curl_easy_cleanup(handle); }

CURLcode CurlLibrary::easy_getinfo_private(CURL *curl, char **user_data) {
  return curl_easy_getinfo(curl, CURLINFO_PRIVATE, user_data);
}

CURLcode CurlLibrary::easy_getinfo_response_code(CURL *curl, long *code) {
  return curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, code);
}

CURLcode CurlLibrary::easy_setopt_errorbuffer(CURL *handle, char *buffer) {
  return curl_easy_setopt(handle, CURLOPT_ERRORBUFFER, buffer);
}

CURLcode CurlLibrary::easy_setopt_headerdata(CURL *handle, void *data) {
  return curl_easy_setopt(handle, CURLOPT_HEADERDATA, data);
}

CURLcode CurlLibrary::easy_setopt_headerfunction(CURL *handle,
                                                 HeaderCallback on_header) {
  return curl_easy_setopt(handle, CURLOPT_HEADERFUNCTION, on_header);
}

CURLcode CurlLibrary::easy_setopt_httpheader(CURL *handle,
                                             curl_slist *headers) {
  return curl_easy_setopt(handle, CURLOPT_HTTPHEADER, headers);
}

CURLcode CurlLibrary::easy_setopt_post(CURL *handle, long post) {
  return curl_easy_setopt(handle, CURLOPT_POST, post);
}

CURLcode CurlLibrary::easy_setopt_postfields(CURL *handle, const char *data) {
  return curl_easy_setopt(handle, CURLOPT_POSTFIELDS, data);
}

CURLcode CurlLibrary::easy_setopt_postfieldsize(CURL *handle, long size) {
  return curl_easy_setopt(handle, CURLOPT_POSTFIELDSIZE, size);
}

CURLcode CurlLibrary::easy_setopt_private(CURL *handle, void *pointer) {
  return curl_easy_setopt(handle, CURLOPT_PRIVATE, pointer);
}

CURLcode CurlLibrary::easy_setopt_unix_socket_path(CURL *handle,
                                                   const char *path) {
  return curl_easy_setopt(handle, CURLOPT_UNIX_SOCKET_PATH, path);
}

CURLcode CurlLibrary::easy_setopt_url(CURL *handle, const char *url) {
  return curl_easy_setopt(handle, CURLOPT_URL, url);
}

CURLcode CurlLibrary::easy_setopt_writedata(CURL *handle, void *data) {
  return curl_easy_setopt(handle, CURLOPT_WRITEDATA, data);
}

CURLcode CurlLibrary::easy_setopt_writefunction(CURL *handle,
                                                WriteCallback on_write) {
  return curl_easy_setopt(handle, CURLOPT_WRITEFUNCTION, on_write);
}

const char *CurlLibrary::easy_strerror(CURLcode error) {
  return curl_easy_strerror(error);
}

void CurlLibrary::global_cleanup() { curl_global_cleanup(); }

CURLcode CurlLibrary::global_init(long flags) {
  return curl_global_init(flags);
}

CURLMcode CurlLibrary::multi_add_handle(CURLM *multi_handle,
                                        CURL *easy_handle) {
  return curl_multi_add_handle(multi_handle, easy_handle);
}

CURLMcode CurlLibrary::multi_cleanup(CURLM *multi_handle) {
  return curl_multi_cleanup(multi_handle);
}

CURLMsg *CurlLibrary::multi_info_read(CURLM *multi_handle, int *msgs_in_queue) {
  return curl_multi_info_read(multi_handle, msgs_in_queue);
}

CURLM *CurlLibrary::multi_init() { return curl_multi_init(); }

CURLMcode CurlLibrary::multi_perform(CURLM *multi_handle,
                                     int *running_handles) {
  return curl_multi_perform(multi_handle, running_handles);
}

CURLMcode CurlLibrary::multi_poll(CURLM *multi_handle, curl_waitfd extra_fds[],
                                  unsigned extra_nfds, int timeout_ms,
                                  int *numfds) {
  return curl_multi_poll(multi_handle, extra_fds, extra_nfds, timeout_ms,
                         numfds);
}

CURLMcode CurlLibrary::multi_remove_handle(CURLM *multi_handle,
                                           CURL *easy_handle) {
  return curl_multi_remove_handle(multi_handle, easy_handle);
}

const char *CurlLibrary::multi_strerror(CURLMcode error) {
  return curl_multi_strerror(error);
}

CURLMcode CurlLibrary::multi_wakeup(CURLM *multi_handle) {
  return curl_multi_wakeup(multi_handle);
}

curl_slist *CurlLibrary::slist_append(curl_slist *list, const char *string) {
  return curl_slist_append(list, string);
}

void CurlLibrary::slist_free_all(curl_slist *list) {
  curl_slist_free_all(list);
}

// `ThreadedCurlEventLoop` is an implementation of `CurlEventLoop` that uses
// libcurl's "multi" interface with a poll set managed by libcurl.
// `ThreadedCurlEventLoop` spawns a thread that waits for I/O events on
// libcurl's poll set, and consumes new request handles enqueued via
// `add_handle`.
class ThreadedCurlEventLoop : public CurlEventLoop {
  // `HandleContext` is everything associated with a `CURL*` handle that was
  // previously passed to `add_handle`. A pointer to `HandleContext` is
  // installed as the `CURL*`'s private data pointer, i.e. `CURLINFO_PRIVATE`.
  struct HandleContext {
    std::function<void(CURLcode) /*noexcept*/> on_error;
    std::function<void() /*noexcept*/> on_done;
    // `user_data` is the `CURLINFO_PRIVATE` property originally associated
    // with the `CURL*, if any.
    void *user_data;
  };

  std::mutex mutex_;
  CurlLibrary &curl_;
  const std::shared_ptr<Logger> logger_;
  CURLM *multi_handle_;
  std::vector<CURL *> new_handles_;
  std::unordered_set<CURL *> handles_;
  bool shutting_down_;
  int num_active_handles_;
  std::condition_variable no_requests_;
  std::thread event_loop_;

 public:
  ThreadedCurlEventLoop(const std::shared_ptr<Logger> &logger,
                        CurlLibrary &curl,
                        const Curl::ThreadGenerator &make_thread);

  Expected<void> add_handle(
      CURL *handle, std::function<void(CURLcode) /*noexcept*/> on_error,
      std::function<void() /*noexcept*/> on_done) override;

  Expected<void> remove_handle(CURL *handle) override;

  void drain(std::chrono::steady_clock::time_point deadline) override;

  ~ThreadedCurlEventLoop() override;

 private:
  void handle_message(std::unique_lock<std::mutex> &, const CURLMsg &);
  CURLcode log_on_error(CURLcode);
  CURLMcode log_on_error(CURLMcode);
  // Remove the `CURL*` referred to by the specified `handle_iter` from
  // `multi_handle_`. Return the context associated with the `CURL*`, and an
  // error if one occurs. The returned context will not be null, even if an
  // error occurs. The behavior is undefined unless all of the following are
  // true:
  // - `lock.owns_lock()`
  // - `handle_iter` refers to an element of `handles_`.
  std::pair<std::unique_ptr<HandleContext>, Expected<void>> remove_handle(
      const std::unique_lock<std::mutex> &lock,
      std::unordered_set<CURL *>::iterator handle_iter);
  void run();
};

std::shared_ptr<ThreadedCurlEventLoop> default_event_loop(
    const std::shared_ptr<Logger> &logger, CurlLibrary &curl,
    const Curl::ThreadGenerator &make_thread) try {
  return std::make_shared<ThreadedCurlEventLoop>(logger, curl, make_thread);
} catch (const std::exception &error) {
  assert(logger);
  logger->log_error(Error{Error::CURL_HTTP_CLIENT_SETUP_FAILED, error.what()});
  return nullptr;
}

auto default_make_thread() {
  return [](auto &&func) {
    return std::thread(std::forward<decltype(func)>(func));
  };
}

ThreadedCurlEventLoop::ThreadedCurlEventLoop(
    const std::shared_ptr<Logger> &logger, CurlLibrary &curl,
    const Curl::ThreadGenerator &make_thread)
    : curl_(curl),
      logger_(logger),
      shutting_down_(false),
      num_active_handles_(0) {
  assert(logger_);

  curl_.global_init(CURL_GLOBAL_ALL);
  multi_handle_ = curl_.multi_init();
  if (multi_handle_ == nullptr) {
    curl_.global_cleanup();
    throw std::runtime_error(
        "Unable to initialize a curl multi-handle for sending requests.");
  }

  try {
    event_loop_ = make_thread([this]() { run(); });
  } catch (...) {
    // Usually the worker thread would do this, but since the thread failed to
    // start, do it here.
    (void)curl_.multi_cleanup(multi_handle_);
    curl_.global_cleanup();
    throw;
  }
}

ThreadedCurlEventLoop::~ThreadedCurlEventLoop() {
  assert(event_loop_.joinable());
  {
    std::lock_guard<std::mutex> lock(mutex_);
    shutting_down_ = true;
  }
  log_on_error(curl_.multi_wakeup(multi_handle_));
  event_loop_.join();
}

void ThreadedCurlEventLoop::run() {
  int num_messages_remaining;
  CURLMsg *message;
  std::unique_lock<std::mutex> lock(mutex_);

  do {
    log_on_error(curl_.multi_perform(multi_handle_, &num_active_handles_));
    if (num_active_handles_ == 0) {
      no_requests_.notify_all();
    }

    // If a request is done or errored out, curl will enqueue a "message" for
    // us to handle.  Handle any pending messages.
    while ((message = curl_.multi_info_read(multi_handle_,
                                            &num_messages_remaining))) {
      handle_message(lock, *message);
    }

    const int max_wait_milliseconds = 10 * 1000;
    lock.unlock();
    log_on_error(curl_.multi_poll(multi_handle_, nullptr, 0,
                                  max_wait_milliseconds, nullptr));
    lock.lock();

    // New requests might have been added while we were sleeping.
    for (CURL *handle : new_handles_) {
      if (log_on_error(curl_.multi_add_handle(multi_handle_, handle)) ==
          CURLM_OK) {
        handles_.insert(handle);
      }
    }
    new_handles_.clear();
  } while (!shutting_down_);

  // We're shutting down.
  while (!handles_.empty()) {
    // TODO: break up remove_handle, this is too weird.
    // TODO: there's a leak here, too. handle's user data is not deleted.
    const auto iter = handles_.begin();
    CURL *const handle = *iter;
    const auto [context, result] = remove_handle(lock, iter);
    if (const Error *error = result.if_error()) {
      logger_->log_error(*error);
    }
    curl_.easy_cleanup(handle);
  }
  log_on_error(curl_.multi_cleanup(multi_handle_));
  curl_.global_cleanup();
}

Expected<void> ThreadedCurlEventLoop::remove_handle(CURL *handle) {
  std::unique_lock<std::mutex> lock(mutex_);
  auto [context, result] = remove_handle(lock, handles_.find(handle));
  return result;
}

std::pair<std::unique_ptr<ThreadedCurlEventLoop::HandleContext>, Expected<void>>
ThreadedCurlEventLoop::remove_handle(
    const std::unique_lock<std::mutex> &lock,
    std::unordered_set<CURL *>::iterator handle_iter) {
  assert(lock.owns_lock());
  (void)lock;  // in case the assert macro doesn't include this "unused" hint

  Expected<void> result;
  const CURLMcode curl_result =
      curl_.multi_remove_handle(multi_handle_, *handle_iter);
  if (curl_result != CURLM_OK) {
    std::string message = "Unable to remove curl handle from event loop: ";
    message += curl_.multi_strerror(curl_result);
    result = Error{Error::CURL_HTTP_CLIENT_EVENT_LOOP_REMOVE_FAILURE,
                   std::move(message)};
  }

  auto *context =
      static_cast<HandleContext *>(get_user_data(curl_, *handle_iter));
  assert(context);
  set_user_data(curl_, *handle_iter, context->user_data);

  handles_.erase(handle_iter);

  return std::make_pair(std::unique_ptr<HandleContext>(context),
                        std::move(result));
}

CURLcode ThreadedCurlEventLoop::log_on_error(CURLcode result) {
  if (result != CURLE_OK) {
    logger_->log_error(
        Error{Error::CURL_HTTP_CLIENT_ERROR, curl_.easy_strerror(result)});
  }
  return result;
}

CURLMcode ThreadedCurlEventLoop::log_on_error(CURLMcode result) {
  if (result != CURLM_OK) {
    logger_->log_error(
        Error{Error::CURL_HTTP_CLIENT_ERROR, curl_.multi_strerror(result)});
  }
  return result;
}

Expected<void> ThreadedCurlEventLoop::add_handle(
    CURL *handle, std::function<void(CURLcode) /*noexcept*/> on_error,
    std::function<void() /*noexcept*/> on_done) {
  auto context = std::make_unique<HandleContext>();
  context->on_error = std::move(on_error);
  context->on_done = std::move(on_done);
  context->user_data = get_user_data(curl_, handle);

  set_user_data(curl_, handle, context.get());

  // `handle` now owns `context.get()`. `context` will relinquish ownership,
  // below, at `context.release()`. If an error occurs before `context`
  // relinquishes context, then `context` will instead delete `context.get()`.
  try {
    // Enqueue `handle` (including with its context, associated with it above).
    std::unique_lock<std::mutex> lock(mutex_);
    new_handles_.push_back(handle);
    context.release();
  } catch (const std::exception &error) {
    // Restore the original user data, if any.
    set_user_data(curl_, handle, context->user_data);
    std::string message = "Unable to enqueue CURL handle: ";
    message += error.what();
    return Error{Error::CURL_REQUEST_SETUP_FAILED, std::move(message)};
  }

  return nullopt;
}

void ThreadedCurlEventLoop::drain(
    std::chrono::steady_clock::time_point deadline) {
  std::unique_lock<std::mutex> lock(mutex_);
  no_requests_.wait_until(lock, deadline, [this]() {
    return num_active_handles_ == 0 && new_handles_.empty();
  });
}

void ThreadedCurlEventLoop::handle_message(std::unique_lock<std::mutex> &lock,
                                           const CURLMsg &message) {
  assert(lock.owns_lock());

  // `CURLMSG_DONE` is the only possible value, but if they were to add another
  // one in the future, we wouldn't know what to do with it.
  if (message.msg != CURLMSG_DONE) {
    return;
  }

  CURL *handle = message.easy_handle;

  // `request` is done.  If we got a response, then call the response
  // handler.  If an error occurred, then call the error handler.
  // First, remove the handle from the curl "multi" handle, and from our
  // bookkeeping, and restore the original user data, if any.
  auto [context, result] = remove_handle(lock, handles_.find(handle));
  if (const Error *error = result.if_error()) {
    logger_->log_error(*error);  // TODO: what if this throws?
  }

  const CURLcode curl_result = message.data.result;
  // Unlock for the duration of the user-specified callback. This allows the
  // callbacks to send more requests (which won't happen, but it'd be senseless
  // to forbid).
  // This is safe because all another thread can do is enqueue another CURL* to
  // be processed by this thread later. There will be no concurrent
  // modifications through any CURL* or CURLM*.
  // Also, both callbacks are `noexcept`, so we don't have to worry about
  // jumping out of this unlocked section.
  // TODO: nope, they aren't noexcept
  lock.unlock();
  if (curl_result != CURLE_OK) {
    context->on_error(curl_result);
  } else {
    context->on_done();
  }
  lock.lock();
}

using ErrorHandler = HTTPClient::ErrorHandler;
using HeadersSetter = HTTPClient::HeadersSetter;
using ResponseHandler = HTTPClient::ResponseHandler;
using URL = HTTPClient::URL;

class CurlImpl {
  CurlLibrary &curl_;
  const std::shared_ptr<CurlEventLoop> event_loop_;
  const std::shared_ptr<Logger> logger_;

  struct Request {
    CurlLibrary *curl = nullptr;
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
    CurlLibrary &curl_;

   public:
    explicit HeaderWriter(CurlLibrary &curl);
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
    Optional<StringView> lookup(StringView key) const override;
    void visit(const std::function<void(StringView key, StringView value)>
                   &visitor) const override;
  };

  CURLcode log_on_error(CURLcode result);

  static std::size_t on_read_header(char *data, std::size_t, std::size_t length,
                                    void *user_data);
  static std::size_t on_read_body(char *data, std::size_t, std::size_t length,
                                  void *user_data);
  static bool is_non_whitespace(unsigned char);
  static char to_lower(unsigned char);
  static StringView trim(StringView);

 public:
  explicit CurlImpl(const std::shared_ptr<Logger> &,
                    const std::shared_ptr<CurlEventLoop> &, CurlLibrary &);

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
    : Curl(logger, default_event_loop(logger, libcurl, default_make_thread()),
           libcurl) {}

Curl::Curl(const std::shared_ptr<Logger> &logger, CurlLibrary &curl)
    : Curl(logger, default_event_loop(logger, curl, default_make_thread()),
           curl) {}

Curl::Curl(const std::shared_ptr<Logger> &logger,
           const std::shared_ptr<CurlEventLoop> &event_loop)
    : impl_(new CurlImpl{logger, event_loop, libcurl}) {}

Curl::Curl(const std::shared_ptr<Logger> &logger,
           const Curl::ThreadGenerator &make_thread, CurlLibrary &curl)
    : Curl(logger, default_event_loop(logger, curl, make_thread), curl) {}

Curl::Curl(const std::shared_ptr<Logger> &logger,
           const std::shared_ptr<CurlEventLoop> &event_loop, CurlLibrary &curl)
    : impl_(new CurlImpl{logger, event_loop, curl}) {}

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

CurlImpl::CurlImpl(const std::shared_ptr<Logger> &logger,
                   const std::shared_ptr<CurlEventLoop> &event_loop,
                   CurlLibrary &curl)
    : curl_(curl), event_loop_(event_loop), logger_(logger) {}

CurlImpl::~CurlImpl() {}

Expected<void> CurlImpl::post(const HTTPClient::URL &url,
                              HeadersSetter set_headers, std::string body,
                              ResponseHandler on_response,
                              ErrorHandler on_error) try {
  if (event_loop_ == nullptr) {
    return Error{Error::CURL_HTTP_CLIENT_NOT_RUNNING,
                 "Unable to send request via libcurl because the HTTP client "
                 "failed to start."};
  }

  auto request = std::make_unique<Request>();

  request->curl = &curl_;
  request->request_body = std::move(body);
  request->on_response = std::move(on_response);
  request->on_error = std::move(on_error);

  auto cleanup_handle = [&](auto handle) { curl_.easy_cleanup(handle); };
  std::unique_ptr<CURL, decltype(cleanup_handle)> handle{
      curl_.easy_init(), std::move(cleanup_handle)};

  if (!handle) {
    return Error{Error::CURL_REQUEST_SETUP_FAILED,
                 "unable to initialize a curl handle for request sending"};
  }

  throw_on_error(curl_.easy_setopt_private(handle.get(), request.get()));
  throw_on_error(
      curl_.easy_setopt_errorbuffer(handle.get(), request->error_buffer));
  throw_on_error(curl_.easy_setopt_post(handle.get(), 1));
  throw_on_error(curl_.easy_setopt_postfieldsize(handle.get(),
                                                 request->request_body.size()));
  throw_on_error(
      curl_.easy_setopt_postfields(handle.get(), request->request_body.data()));
  throw_on_error(
      curl_.easy_setopt_headerfunction(handle.get(), &on_read_header));
  throw_on_error(curl_.easy_setopt_headerdata(handle.get(), request.get()));
  throw_on_error(curl_.easy_setopt_writefunction(handle.get(), &on_read_body));
  throw_on_error(curl_.easy_setopt_writedata(handle.get(), request.get()));
  if (url.scheme == "unix" || url.scheme == "http+unix" ||
      url.scheme == "https+unix") {
    throw_on_error(curl_.easy_setopt_unix_socket_path(handle.get(),
                                                      url.authority.c_str()));
    // The authority section of the URL is ignored when a unix domain socket is
    // to be used.
    throw_on_error(curl_.easy_setopt_url(
        handle.get(), ("http://localhost" + url.path).c_str()));
  } else {
    throw_on_error(curl_.easy_setopt_url(
        handle.get(), (url.scheme + "://" + url.authority + url.path).c_str()));
  }

  HeaderWriter writer{curl_};
  set_headers(writer);
  auto cleanup_list = [&](auto list) { curl_.slist_free_all(list); };
  std::unique_ptr<curl_slist, decltype(cleanup_list)> headers{
      writer.release(), std::move(cleanup_list)};
  request->request_headers = headers.get();
  throw_on_error(
      curl_.easy_setopt_httpheader(handle.get(), request->request_headers));

  auto event_loop_on_error = [this,
                              handle = handle.get()](CURLcode result) noexcept {
    auto request = static_cast<Request *>(get_user_data(curl_, handle));
    std::string error_message;
    error_message += "Error sending request with libcurl (";
    error_message += curl_.easy_strerror(result);
    error_message += "): ";
    error_message += request->error_buffer;
    try {
      request->on_error(
          Error{Error::CURL_REQUEST_FAILURE, std::move(error_message)});
    } catch (const std::exception &error) {
      logger_->log_error(
          Error{Error::HTTP_CLIENT_ON_ERROR_HANDLER_EXCEPTION, error.what()});
    }

    delete request;
    curl_.easy_cleanup(handle);
  };

  auto event_loop_on_done = [this, handle = handle.get()]() noexcept {
    long status;
    if (log_on_error(curl_.easy_getinfo_response_code(handle, &status)) !=
        CURLE_OK) {
      status = -1;
    }
    auto request = static_cast<Request *>(get_user_data(curl_, handle));
    HeaderReader reader(&request->response_headers_lower);
    try {
      request->on_response(static_cast<int>(status), reader,
                           std::move(request->response_body));
    } catch (const std::exception &error) {
      logger_->log_error(
          Error{Error::HTTP_CLIENT_ON_DONE_HANDLER_EXCEPTION, error.what()});
    }

    delete request;
    curl_.easy_cleanup(handle);
  };

  auto result = event_loop_->add_handle(handle.get(), event_loop_on_error,
                                        event_loop_on_done);
  if (!result) {
    return result;
  }

  headers.release();
  handle.release();
  request.release();

  return nullopt;
} catch (CURLcode error) {
  return Error{Error::CURL_REQUEST_SETUP_FAILED, curl_.easy_strerror(error)};
}

void CurlImpl::drain(std::chrono::steady_clock::time_point deadline) {
  if (!event_loop_) {
    return;
  }
  event_loop_->drain(deadline);
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
        Error{Error::CURL_HTTP_CLIENT_ERROR, curl_.easy_strerror(result)});
  }
  return result;
}

CurlImpl::Request::~Request() { curl->slist_free_all(request_headers); }

CurlImpl::HeaderWriter::HeaderWriter(CurlLibrary &curl) : curl_(curl) {}

CurlImpl::HeaderWriter::~HeaderWriter() { curl_.slist_free_all(list_); }

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

  list_ = curl_.slist_append(list_, buffer_.c_str());
}

CurlImpl::HeaderReader::HeaderReader(
    std::unordered_map<std::string, std::string> *response_headers_lower)
    : response_headers_lower_(response_headers_lower) {}

Optional<StringView> CurlImpl::HeaderReader::lookup(StringView key) const {
  buffer_.clear();
  std::transform(key.begin(), key.end(), std::back_inserter(buffer_),
                 &to_lower);

  const auto found = response_headers_lower_->find(buffer_);
  if (found == response_headers_lower_->end()) {
    return nullopt;
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
