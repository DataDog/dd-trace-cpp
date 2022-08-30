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
// environment variable name, but without the leading "DD_".
enum Variable {
  AGENT_HOST,
  ENV,
  PROPAGATION_STYLE_EXTRACT,
  PROPAGATION_STYLE_INJECT,
  SERVICE,
  SPAN_SAMPLING_RULES,
  SPAN_SAMPLING_RULES_FILE,
  TAGS,
  TRACE_AGENT_PORT,
  TRACE_AGENT_URL,
  TRACE_ANALYTICS_ENABLED,
  TRACE_ANALYTICS_SAMPLE_RATE,
  TRACE_CPP_LEGACY_OBFUSCATION,
  TRACE_DEBUG,
  TRACE_ENABLED,
  TRACE_RATE_LIMIT,
  TRACE_REPORT_HOSTNAME,
  TRACE_SAMPLE_RATE,
  TRACE_SAMPLING_RULES,
  TRACE_STARTUP_LOGS,
  TRACE_TAGS_PROPAGATION_MAX_LENGTH,
  VERSION,
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
    "DD_TRACE_ANALYTICS_ENABLED",
    "DD_TRACE_ANALYTICS_SAMPLE_RATE",
    "DD_TRACE_CPP_LEGACY_OBFUSCATION",
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

std::optional<std::string_view> lookup(Variable variable);

}  // namespace environment
}  // namespace tracing
}  // namespace datadog
