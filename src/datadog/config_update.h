// TODO: Document

#include "optional"
#include "trace_sampler.h"

namespace datadog {
namespace tracing {

struct ConfigUpdate {
  Optional<TraceSamplerConfig> trace_sampler;
};

}  // namespace tracing
}  // namespace datadog
