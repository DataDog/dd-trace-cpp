#pragma once

// TODO: Document

#include "optional"
#include "trace_sampler_config.h"

namespace datadog {
namespace tracing {

struct ConfigUpdate {
  Optional<TraceSamplerConfig> trace_sampler;
};

}  // namespace tracing
}  // namespace datadog
