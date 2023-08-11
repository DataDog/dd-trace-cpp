#pragma once

// TODO

#include <string>

#include "expected.h"

namespace datadog {
namespace tracing {

struct TracerDebugConfig {
  // `enabled` indicates whether debug traces are to be created.
  // This value is overridden by the `DD_TRACE_DEBUG_TRACES` environment
  // variable.
  bool enabled = false;
  // `service` is the service name for spans within debug traces.
  std::string service = "dd-trace-cpp-debug";
};

class FinalizedTracerDebugConfig {
  friend Expected<FinalizedTracerDebugConfig> finalize_config(
      const TracerDebugConfig&);
  friend class FinalizedTracerConfig;
  FinalizedTracerDebugConfig() = default;

 public:
  bool enabled;
  std::string service;
};

Expected<FinalizedTracerDebugConfig> finalize_config(const TracerDebugConfig&);

}  // namespace tracing
}  // namespace datadog
