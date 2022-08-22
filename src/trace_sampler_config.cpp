#include "trace_sampler_config.h"
namespace datadog {
namespace tracing {

// TODO

std::variant<Validated<TraceSamplerConfig>, Error> validate_config(
    const TraceSamplerConfig& config) {
  // TODO
  return Validated<TraceSamplerConfig>{config};
}

}  // namespace tracing
}  // namespace datadog
