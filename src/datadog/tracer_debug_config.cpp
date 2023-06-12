#include "tracer_debug_config.h"

namespace datadog {
namespace tracing {

Expected<FinalizedTracerDebugConfig> finalize_config(
    const TracerDebugConfig& config) {
  // For now, no error can occur, and no environment variable can override.
  // So, just copy the fields from the config into the finalized config.
  FinalizedTracerDebugConfig result;
  result.enabled = config.enabled;
  result.service = config.service;
  return result;
}

}  // namespace tracing
}  // namespace datadog
