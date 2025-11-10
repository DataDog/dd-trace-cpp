#pragma once

#include <datadog/string_view.h>

#include <string>

namespace datadog::tracing {

// Infer the endpoint pattern from a URL path by replacing parameters with
// placeholders like {param:int}, {param:hex}, etc.
//
// The input should be a clean path without query string (e.g.,
// "/api/users/123"). URL parsing should be handled by the caller using
// HTTPClient::URL::parse().
std::string infer_endpoint(StringView path);

}  // namespace datadog::tracing
