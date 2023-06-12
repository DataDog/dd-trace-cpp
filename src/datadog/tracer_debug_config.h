#pragma once

// TODO

#include <memory>
#include <string>

#include "expected.h"
#include "logger.h"

namespace datadog {
namespace tracing {

struct TracerDebugConfig {
  // `enabled` indicates whether debug traces are to be created.
  bool enabled = false;
  // `service` is the service name for spans within debug traces.
  std::string service = "dd-trace-cpp-debug";
  // If `logger` is not `nullptr`, then debug traces, in addition to being
  // sent to a `Collector`, will also be printed to the specified `logger`.
  std::shared_ptr<Logger> logger;
};

class FinalizedTracerDebugConfig {
  friend Expected<FinalizedTracerDebugConfig> finalize_config(
      const TracerDebugConfig&);
  friend class FinalizedTracerConfig;
  FinalizedTracerDebugConfig() = default;

 public:
  bool enabled;
  std::string service;
  std::shared_ptr<Logger> logger;
};

Expected<FinalizedTracerDebugConfig> finalize_config(const TracerDebugConfig&);

}  // namespace tracing
}  // namespace datadog
