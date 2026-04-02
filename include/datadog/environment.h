#pragma once

// This component provides a registry of all environment variables that can be
// used to configure this library.
//
// Each `enum Variable` denotes an environment variable.  The enum value names
// are the same as the names of the environment variables.
//
// `name` returns the name of a specified `Variable`.
//
// `lookup` retrieves the value of `Variable` in the environment.
#include <datadog/optional.h>
#include <datadog/string_view.h>

#include <string>

namespace datadog {
namespace tracing {
namespace environment {

// Central registry for supported environment variables.
// All configurations must be registered here.
// See also:
// https://feature-parity.us1.prod.dog/configurations?viewType=configurations
//
// This registry is the single source of truth for:
//   - env variable name allowlist (`include/datadog/environment.h`)
//   - generated metadata (`metadata/supported-configurations.json`)
//
// Each entry has:
//   - NAME:    environment variable symbol (e.g. DD_SERVICE)
//   - TYPE:    STRING | BOOLEAN | INT | DECIMAL | ARRAY | MAP
//   - DEFAULT: literal default value or a marker token
//
// Marker tokens:
//   - ENV_DEFAULT_RESOLVED_IN_CODE("...description...")
//       The runtime default is resolved in C++ configuration finalization
//       logic. The description is emitted as the "default" field in
//       metadata/supported-configurations.json.
#define DD_LIST_ENVIRONMENT_VARIABLES(MACRO)                                   \
  MACRO(DD_AGENT_HOST, STRING, "localhost")                                    \
  MACRO(DD_ENV, STRING, "")                                                    \
  MACRO(DD_INSTRUMENTATION_TELEMETRY_ENABLED, BOOLEAN, true)                   \
  MACRO(DD_PROPAGATION_STYLE_EXTRACT, ARRAY, "datadog,tracecontext,baggage")   \
  MACRO(DD_PROPAGATION_STYLE_INJECT, ARRAY, "datadog,tracecontext,baggage")    \
  MACRO(DD_REMOTE_CONFIGURATION_ENABLED, BOOLEAN, true)                        \
  MACRO(DD_REMOTE_CONFIG_POLL_INTERVAL_SECONDS, DECIMAL, 5.0)                  \
  MACRO(DD_SERVICE, STRING,                                                    \
        ENV_DEFAULT_RESOLVED_IN_CODE("Defaults to process name when unset."))  \
  MACRO(DD_SPAN_SAMPLING_RULES, ARRAY, "[]")                                   \
  MACRO(DD_SPAN_SAMPLING_RULES_FILE, STRING, "")                               \
  MACRO(DD_TRACE_PROPAGATION_STYLE_EXTRACT, ARRAY,                             \
        "datadog,tracecontext,baggage")                                        \
  MACRO(DD_TRACE_PROPAGATION_STYLE_INJECT, ARRAY,                              \
        "datadog,tracecontext,baggage")                                        \
  MACRO(DD_TRACE_PROPAGATION_STYLE, ARRAY, "datadog,tracecontext,baggage")     \
  MACRO(DD_TAGS, MAP, "")                                                      \
  MACRO(DD_TRACE_AGENT_PORT, INT, 8126)                                        \
  MACRO(DD_TRACE_AGENT_URL, STRING,                                            \
        ENV_DEFAULT_RESOLVED_IN_CODE(                                          \
            "If unset, built from DD_AGENT_HOST and DD_TRACE_AGENT_PORT, "     \
            "then defaults to http://localhost:8126."))                        \
  MACRO(DD_TRACE_DEBUG, BOOLEAN, false)                                        \
  MACRO(DD_TRACE_ENABLED, BOOLEAN, true)                                       \
  MACRO(DD_TRACE_RATE_LIMIT, DECIMAL, 100.0)                                   \
  MACRO(DD_TRACE_REPORT_HOSTNAME, BOOLEAN, false)                              \
  MACRO(DD_TRACE_SAMPLE_RATE, DECIMAL, 1.0)                                    \
  MACRO(DD_TRACE_SAMPLING_RULES, ARRAY, "[]")                                  \
  MACRO(DD_TRACE_STARTUP_LOGS, BOOLEAN, true)                                  \
  MACRO(DD_TRACE_TAGS_PROPAGATION_MAX_LENGTH, INT, 512)                        \
  MACRO(DD_VERSION, STRING, "")                                                \
  MACRO(DD_TRACE_128_BIT_TRACEID_GENERATION_ENABLED, BOOLEAN, true)            \
  MACRO(DD_TELEMETRY_HEARTBEAT_INTERVAL, DECIMAL, 10)                          \
  MACRO(DD_TELEMETRY_METRICS_ENABLED, BOOLEAN, true)                           \
  MACRO(DD_TELEMETRY_METRICS_INTERVAL_SECONDS, DECIMAL, 60)                    \
  MACRO(DD_TELEMETRY_DEBUG, BOOLEAN, false)                                    \
  MACRO(DD_TRACE_BAGGAGE_MAX_ITEMS, INT, 64)                                   \
  MACRO(DD_TRACE_BAGGAGE_MAX_BYTES, INT, 8192)                                 \
  MACRO(DD_TELEMETRY_LOG_COLLECTION_ENABLED, BOOLEAN, true)                    \
  MACRO(DD_INSTRUMENTATION_INSTALL_ID, STRING, "")                             \
  MACRO(DD_INSTRUMENTATION_INSTALL_TYPE, STRING, "")                           \
  MACRO(DD_INSTRUMENTATION_INSTALL_TIME, STRING, "")                           \
  MACRO(DD_APM_TRACING_ENABLED, BOOLEAN, true)                                 \
  MACRO(DD_TRACE_RESOURCE_RENAMING_ENABLED, BOOLEAN, false)                    \
  MACRO(DD_TRACE_RESOURCE_RENAMING_ALWAYS_SIMPLIFIED_ENDPOINT, BOOLEAN, false) \
  MACRO(DD_EXTERNAL_ENV, STRING, "")                                           \
  /* Internal: session ID for stable telemetry headers. Not user-facing. */    \
  MACRO(_DD_ROOT_CPP_SESSION_ID, STRING, "")

#define ENV_DEFAULT_RESOLVED_IN_CODE(X) X
#define WITH_COMMA(ARG, TYPE, DEFAULT_VALUE) ARG,

enum Variable { DD_LIST_ENVIRONMENT_VARIABLES(WITH_COMMA) };

// Quoting a macro argument requires this two-step.
#define QUOTED_IMPL(ARG) #ARG
#define QUOTED(ARG) QUOTED_IMPL(ARG)

#define QUOTED_WITH_COMMA(ARG, TYPE, DEFAULT_VALUE) \
  WITH_COMMA(QUOTED(ARG), TYPE, DEFAULT_VALUE)

inline const char* const variable_names[] = {
    DD_LIST_ENVIRONMENT_VARIABLES(QUOTED_WITH_COMMA)};

#undef QUOTED
#undef QUOTED_IMPL
#undef WITH_COMMA
#undef ENV_DEFAULT_RESOLVED_IN_CODE

// Return the name of the specified environment `variable`.
StringView name(Variable variable);

// Return the value of the specified environment `variable`, or return
// `nullopt` if that variable is not set in the environment.
Optional<StringView> lookup(Variable variable);

// Set the specified environment `variable` to `value`. Does not overwrite if
// already set (equivalent to setenv(..., 0) on POSIX).
void set_if_not_set(Variable variable, StringView value);

std::string to_json();

}  // namespace environment
}  // namespace tracing
}  // namespace datadog
