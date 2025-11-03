#pragma once

#include <string>
#include <string_view>

namespace datadog::tracing {

std::string guess_endpoint(std::string_view path);

}
