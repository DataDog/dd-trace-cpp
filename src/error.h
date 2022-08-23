#pragma once

#include <iosfwd>
#include <string>

namespace datadog {
namespace tracing {

struct Error;

std::ostream& operator<<(std::ostream&, const Error&);

struct Error {
  int code;
  std::string message;

  std::string to_string() const;

  enum {
    // TODO: enumerating all of the error codes means that
    // every time I add an error to a component, I have to recompile
    // all components.
    SERVICE_NAME_REQUIRED = 1,
    MESSAGEPACK_ENCODE_FAILURE = 2,
    CURL_REQUEST_FAILURE = 3,
    DATADOG_AGENT_NULL_HTTP_CLIENT = 4,
    DATADOG_AGENT_INVALID_FLUSH_INTERVAL = 5,
    NULL_COLLECTOR = 6,
    URL_MISSING_SEPARATOR = 7,
    URL_UNSUPPORTED_SCHEME = 8,
    URL_UNIX_DOMAIN_SOCKET_PATH_NOT_ABSOLUTE = 9,
  };
};

}  // namespace tracing
}  // namespace datadog
