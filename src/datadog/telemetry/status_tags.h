#pragma once

// The `status_code` tag of the metrics that count HTTP responses.

#include <datadog/optional.h>
#include <datadog/telemetry/metric_handle.h>

#include <array>
#include <tuple>

namespace datadog::telemetry {

enum class StatusClass {
  informational,
  success,
  redirection,
  client_error,
  server_error
};

constexpr std::array<const char*, 5> tag_value_names(StatusClass) {
  return {"1xx", "2xx", "3xx", "4xx", "5xx"};
}

struct StatusTags {
  StatusClass status_code = StatusClass::success;

  // The class of an HTTP status, such as 4xx for 404. Empty for a status
  // below 100.
  static tracing::Optional<StatusTags> from_http_status(int status) {
    if (status >= 500) return StatusTags{StatusClass::server_error};
    if (status >= 400) return StatusTags{StatusClass::client_error};
    if (status >= 300) return StatusTags{StatusClass::redirection};
    if (status >= 200) return StatusTags{StatusClass::success};
    if (status >= 100) return StatusTags{StatusClass::informational};
    return tracing::nullopt;
  }

  static auto tag_fields() {
    return std::tuple{tag("status_code", &StatusTags::status_code)};
  }
};

}  // namespace datadog::telemetry
