#pragma once

#include <memory>

namespace datadog {
namespace tracing {

class HTTPClient;
class Logger;

std::shared_ptr<HTTPClient> default_http_client(
    const std::shared_ptr<Logger>& logger);

}  // namespace tracing
}  // namespace datadog
