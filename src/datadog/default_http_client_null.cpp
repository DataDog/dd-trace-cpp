#include "default_http_client.h"

namespace datadog {
namespace tracing {

std::shared_ptr<HTTPClient> default_http_client(
    const std::shared_ptr<Logger>&) {
  return nullptr;
}

}  // namespace tracing
}  // namespace datadog
