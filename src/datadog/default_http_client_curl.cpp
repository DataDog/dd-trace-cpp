#include "curl.h"
#include "default_http_client.h"

namespace datadog {
namespace tracing {

std::shared_ptr<HTTPClient> default_http_client(
    const std::shared_ptr<Logger>& logger) {
  return std::make_shared<Curl>(logger);
}

}  // namespace tracing
}  // namespace datadog
