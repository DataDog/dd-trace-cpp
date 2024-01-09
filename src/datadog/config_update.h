#pragma once

#include "optional"
#include "trace_sampler_config.h"

namespace datadog {
namespace tracing {

// The `ConfigUpdate` struct serves as a container for configuration that can
// exclusively be changed remotely.
//
// Configurations can be `nullopt` to signal the absence of a value from the
// remote configuration value.
struct ConfigUpdate {
  Optional<TraceSamplerConfig> trace_sampler;
};

}  // namespace tracing
}  // namespace datadog
