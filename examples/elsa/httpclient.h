#pragma once

#include <datadog/dict_writer.h>
#include <datadog/http_client.h>

#include "httplib.h"

struct HttplibClient : public datadog::tracing::HTTPClient {
  class HeaderWriter final : public datadog::tracing::DictWriter {
    httplib::Headers& headers_;

   public:
    explicit HeaderWriter(httplib::Headers& headers) : headers_(headers) {}

    void set(std::string_view key, std::string_view value) override {
      auto found = headers_.find(std::string(key));
      if (found == headers_.cend()) {
        headers_.emplace(key, value);
      } else {
        found->second = value;
      }
    }
  };
  httplib::Client cli;

  HttplibClient(std::string_view agent_url) : cli(agent_url.data()) {}

  datadog::tracing::Expected<void> post(
      const URL& url, HeadersSetter set_headers, std::string body,
      ResponseHandler on_response, ErrorHandler on_error,
      std::chrono::steady_clock::time_point deadline) override {
    httplib::Request req;
    req.method = "POST";
    req.path = url.path;
    req.body = body;

    HeaderWriter writer(req.headers);
    set_headers(writer);

    auto result = cli.send(req);
    if (result.error() != httplib::Error::Success) {
      on_error(datadog::tracing::Error{
          datadog::tracing::Error::Code::CURL_HTTP_CLIENT_ERROR,
          to_string(result.error())});
    }
    return {};
  }

  // Wait until there are no more outstanding requests, or until the specified
  // `deadline`.
  inline void drain(std::chrono::steady_clock::time_point deadline) override {
    return;
  };

  // Return a JSON representation of this object's configuration. The JSON
  // representation is an object with the following properties:
  //
  // - "type" is the unmangled, qualified name of the most-derived class, e.g.
  //   "datadog::tracing::Curl".
  // - "config" is an object containing this object's configuration. "config"
  //   may be omitted if the derived class has no configuration.
  inline std::string config() const override {
    return "{\"type\": \"httplib\"}";
  };

  ~HttplibClient() = default;
};
