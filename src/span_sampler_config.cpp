#include "span_sampler_config.h"

namespace datadog {
namespace tracing {

Expected<FinalizedSpanSamplerConfig> finalize_config(const SpanSamplerConfig&) {
  // TODO
  return FinalizedSpanSamplerConfig{};
}

}  // namespace tracing
}  // namespace datadog
