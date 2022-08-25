#pragma once

#include <iosfwd>
#include <string>
#include <string_view>

namespace datadog {
namespace tracing {

struct Error;

std::ostream& operator<<(std::ostream&, const Error&);

struct Error {
  int code;
  std::string message;

  std::string to_string() const;
  Error with_prefix(std::string_view) const;

  enum {
    SERVICE_NAME_REQUIRED = 1,
    MESSAGEPACK_ENCODE_FAILURE = 2,
    CURL_REQUEST_FAILURE = 3,
    DATADOG_AGENT_NULL_HTTP_CLIENT = 4,
    DATADOG_AGENT_INVALID_FLUSH_INTERVAL = 5,
    NULL_COLLECTOR = 6,
    URL_MISSING_SEPARATOR = 7,
    URL_UNSUPPORTED_SCHEME = 8,
    URL_UNIX_DOMAIN_SOCKET_PATH_NOT_ABSOLUTE = 9,
    NO_SPAN_TO_EXTRACT = 10,
    NOT_IMPLEMENTED = 11,
    MISSING_SPAN_INJECTION_STYLE = 12,
    MISSING_SPAN_EXTRACTION_STYLE = 13,
    OUT_OF_RANGE_INTEGER = 14,
    INVALID_INTEGER = 15,
    MISSING_PARENT_SPAN_ID = 16,
  };
};

}  // namespace tracing
}  // namespace datadog
