#pragma once

#include <functional>
#include <optional>
#include <string_view>

#include "error.h"

namespace datadog {
namespace tracing {

class DictReader;
class DictWriter;

class HTTPClient {
 public:
  using HeadersSetter = std::function<void(DictWriter& headers)>;
  using ResponseHandler = std::function<void(
      int status, const DictReader& headers, std::string body)>;
  // `ErrorHandler` is for errors encountered by `HTTPClient`, not for
  // error-indicating HTTP responses.
  using ErrorHandler = std::function<void(Error)>;

  virtual std::optional<Error> send_request(std::string_view URL,
                                            HeadersSetter set_headers,
                                            std::string body,
                                            ResponseHandler on_response,
                                            ErrorHandler on_error) = 0;

  virtual ~HTTPClient();
};

}  // namespace tracing
}  // namespace datadog
