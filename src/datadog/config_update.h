#include "optional"
#include "trace_sampler.h"

namespace datadog {
namespace tracing {

// TODO: Document
struct ConfigUpdate {
  Optional<TraceSamplerConfig> trace_sampler;
};

}  // namespace tracing
}  // namespace datadog
