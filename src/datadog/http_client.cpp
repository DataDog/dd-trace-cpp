#include "http_client.h"

#include <ostream>

namespace datadog {
namespace tracing {

std::ostream& operator<<(std::ostream& stream, const HTTPClient::URL& url) {
  return stream << url.scheme << "://" << url.authority << url.path;
}

}  // namespace tracing
}  // namespace datadog
