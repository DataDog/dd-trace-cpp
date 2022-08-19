#include "validated_tracer_config.h"

#include <utility>

namespace datadog {
namespace tracing {

ValidatedTracerConfig::ValidatedTracerConfig(const TracerConfig& before_env,
                                             TracerConfig&& after_env)
    : before_env_(before_env), after_env_(std::move(after_env)) {}

}  // namespace tracing
}  // namespace datadog
