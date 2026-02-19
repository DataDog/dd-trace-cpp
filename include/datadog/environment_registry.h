#pragma once

// Central registry for supported environment variables.
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
//
// This registry is the single source of truth for:
//   - env variable name allowlist (`include/datadog/environment.h`)
//   - generated metadata (`metadata/supported-configurations.json`)

#define DD_ENVIRONMENT_VARIABLES(MACRO, DATA)                                 \
  MACRO(DATA, DD_AGENT_HOST, STRING, "localhost")                             \
  MACRO(DATA, DD_ENV, STRING, "")                                             \
  MACRO(DATA, DD_INSTRUMENTATION_TELEMETRY_ENABLED, BOOLEAN, true)            \
  MACRO(DATA, DD_PROPAGATION_STYLE_EXTRACT, ARRAY,                            \
        "datadog,tracecontext,baggage")                                       \
  MACRO(DATA, DD_PROPAGATION_STYLE_INJECT, ARRAY,                             \
        "datadog,tracecontext,baggage")                                       \
  MACRO(DATA, DD_REMOTE_CONFIGURATION_ENABLED, BOOLEAN, true)                 \
  MACRO(DATA, DD_REMOTE_CONFIG_POLL_INTERVAL_SECONDS, DECIMAL, 5.0)           \
  MACRO(DATA, DD_SERVICE, STRING,                                             \
        ENV_DEFAULT_RESOLVED_IN_CODE("Defaults to process name when unset.")) \
  MACRO(DATA, DD_SPAN_SAMPLING_RULES, ARRAY, "[]")                            \
  MACRO(DATA, DD_SPAN_SAMPLING_RULES_FILE, STRING, "")                        \
  MACRO(DATA, DD_TRACE_PROPAGATION_STYLE_EXTRACT, ARRAY,                      \
        "datadog,tracecontext,baggage")                                       \
  MACRO(DATA, DD_TRACE_PROPAGATION_STYLE_INJECT, ARRAY,                       \
        "datadog,tracecontext,baggage")                                       \
  MACRO(DATA, DD_TRACE_PROPAGATION_STYLE, ARRAY,                              \
        "datadog,tracecontext,baggage")                                       \
  MACRO(DATA, DD_TAGS, MAP, "")                                               \
  MACRO(DATA, DD_TRACE_AGENT_PORT, INT, 8126)                                 \
  MACRO(DATA, DD_TRACE_AGENT_URL, STRING,                                     \
        ENV_DEFAULT_RESOLVED_IN_CODE(                                         \
            "If unset, built from DD_AGENT_HOST and DD_TRACE_AGENT_PORT, "    \
            "then defaults to http://localhost:8126."))                       \
  MACRO(DATA, DD_TRACE_DEBUG, BOOLEAN, false)                                 \
  MACRO(DATA, DD_TRACE_ENABLED, BOOLEAN, true)                                \
  MACRO(DATA, DD_TRACE_RATE_LIMIT, DECIMAL, 100.0)                            \
  MACRO(DATA, DD_TRACE_REPORT_HOSTNAME, BOOLEAN, false)                       \
  MACRO(DATA, DD_TRACE_SAMPLE_RATE, DECIMAL, 1.0)                             \
  MACRO(DATA, DD_TRACE_SAMPLING_RULES, ARRAY, "[]")                           \
  MACRO(DATA, DD_TRACE_STARTUP_LOGS, BOOLEAN, true)                           \
  MACRO(DATA, DD_TRACE_TAGS_PROPAGATION_MAX_LENGTH, INT, 512)                 \
  MACRO(DATA, DD_VERSION, STRING, "")                                         \
  MACRO(DATA, DD_TRACE_128_BIT_TRACEID_GENERATION_ENABLED, BOOLEAN, true)     \
  MACRO(DATA, DD_TELEMETRY_HEARTBEAT_INTERVAL, DECIMAL, 10)                   \
  MACRO(DATA, DD_TELEMETRY_METRICS_ENABLED, BOOLEAN, true)                    \
  MACRO(DATA, DD_TELEMETRY_METRICS_INTERVAL_SECONDS, DECIMAL, 60)             \
  MACRO(DATA, DD_TELEMETRY_DEBUG, BOOLEAN, false)                             \
  MACRO(DATA, DD_TRACE_BAGGAGE_MAX_ITEMS, INT, 64)                            \
  MACRO(DATA, DD_TRACE_BAGGAGE_MAX_BYTES, INT, 8192)                          \
  MACRO(DATA, DD_TELEMETRY_LOG_COLLECTION_ENABLED, BOOLEAN, true)             \
  MACRO(DATA, DD_INSTRUMENTATION_INSTALL_ID, STRING, "")                      \
  MACRO(DATA, DD_INSTRUMENTATION_INSTALL_TYPE, STRING, "")                    \
  MACRO(DATA, DD_INSTRUMENTATION_INSTALL_TIME, STRING, "")                    \
  MACRO(DATA, DD_APM_TRACING_ENABLED, BOOLEAN, true)                          \
  MACRO(DATA, DD_TRACE_RESOURCE_RENAMING_ENABLED, BOOLEAN, false)             \
  MACRO(DATA, DD_TRACE_RESOURCE_RENAMING_ALWAYS_SIMPLIFIED_ENDPOINT, BOOLEAN, \
        false)                                                                \
  MACRO(DATA, DD_EXTERNAL_ENV, STRING, "")
