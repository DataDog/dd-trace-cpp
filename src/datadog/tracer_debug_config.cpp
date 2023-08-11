#include "tracer_debug_config.h"

#include "environment.h"
#include "parse_util.h"

namespace datadog {
namespace tracing {

Expected<FinalizedTracerDebugConfig> finalize_config(
    const TracerDebugConfig& config) {
  FinalizedTracerDebugConfig result;
  result.enabled = config.enabled;
  if (const auto debug_env = lookup(environment::DD_TRACE_DEBUG_TRACES)) {
    result.enabled = !falsy(*debug_env);
  }

  result.service = config.service;
  return result;
}

}  // namespace tracing
}  // namespace datadog
