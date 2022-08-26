#include "default_http_client.h"

namespace datadog {
namespace tracing {

std::shared_ptr<HTTPClient> default_http_client() { return nullptr; }

}  // namespace tracing
}  // namespace datadog
