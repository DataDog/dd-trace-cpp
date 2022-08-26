#pragma once

#include <variant>

#include "expected.h"

namespace datadog {
namespace tracing {

struct SpanSamplerConfig {
  // TODO
};

class FinalizedSpanSamplerConfig {
  friend Expected<FinalizedSpanSamplerConfig> finalize_config(
      const SpanSamplerConfig& config);

  FinalizedSpanSamplerConfig() = default;

 public:
  // TODO
};

Expected<FinalizedSpanSamplerConfig> finalize_config(
    const SpanSamplerConfig& config);

}  // namespace tracing
}  // namespace datadog
