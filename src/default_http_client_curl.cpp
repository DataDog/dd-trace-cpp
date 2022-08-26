#include "curl.h"
#include "default_http_client.h"

namespace datadog {
namespace tracing {

std::shared_ptr<HTTPClient> default_http_client() {
  return std::make_shared<Curl>();
}

}  // namespace tracing
}  // namespace datadog
