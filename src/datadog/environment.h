#pragma once

#include <optional>
#include <string_view>

namespace datadog {
namespace tracing {
namespace environment {

// Keep this sorted.  The values must correspond to offsets within
// `variable_names`.
// To ensure that the sorted `enum` names corresponding to the sorted
// `variable_names`, each `enum` name must be equal to the corresponding
// environment variable name.
enum Variable {
  DD_AGENT_HOST,
  DD_ENV,
  DD_PROPAGATION_STYLE_EXTRACT,
  DD_PROPAGATION_STYLE_INJECT,
  DD_SERVICE,
  DD_SPAN_SAMPLING_RULES,
  DD_SPAN_SAMPLING_RULES_FILE,
  DD_TAGS,
  DD_TRACE_AGENT_PORT,
  DD_TRACE_AGENT_URL,
  DD_TRACE_DEBUG,
  DD_TRACE_ENABLED,
  DD_TRACE_RATE_LIMIT,
  DD_TRACE_REPORT_HOSTNAME,
  DD_TRACE_SAMPLE_RATE,
  DD_TRACE_SAMPLING_RULES,
  DD_TRACE_STARTUP_LOGS,
  DD_TRACE_TAGS_PROPAGATION_MAX_LENGTH,
  DD_VERSION,
};

// Keep this sorted.  Offsets into this array are indicated by `Variable`
// values.
inline const char *const variable_names[] = {
    "DD_AGENT_HOST",
    "DD_ENV",
    "DD_PROPAGATION_STYLE_EXTRACT",
    "DD_PROPAGATION_STYLE_INJECT",
    "DD_SERVICE",
    "DD_SPAN_SAMPLING_RULES",
    "DD_SPAN_SAMPLING_RULES_FILE",
    "DD_TAGS",
    "DD_TRACE_AGENT_PORT",
    "DD_TRACE_AGENT_URL",
    "DD_TRACE_DEBUG",
    "DD_TRACE_ENABLED",
    "DD_TRACE_RATE_LIMIT",
    "DD_TRACE_REPORT_HOSTNAME",
    "DD_TRACE_SAMPLE_RATE",
    "DD_TRACE_SAMPLING_RULES",
    "DD_TRACE_STARTUP_LOGS",
    "DD_TRACE_TAGS_PROPAGATION_MAX_LENGTH",
    "DD_VERSION",
};

std::string_view name(Variable variable);

std::optional<std::string_view> lookup(Variable variable);

}  // namespace environment
}  // namespace tracing
}  // namespace datadog
