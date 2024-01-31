#pragma once

#include <unordered_map>

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
  Optional<std::unordered_map<std::string, std::string>> tags;
};

}  // namespace tracing
}  // namespace datadog
