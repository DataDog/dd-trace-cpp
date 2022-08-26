#pragma once

#include <memory>

namespace datadog {
namespace tracing {

class HTTPClient;

std::shared_ptr<HTTPClient> default_http_client();

}  // namespace tracing
}  // namespace datadog
