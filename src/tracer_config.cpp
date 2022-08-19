#include "tracer_config.h"

namespace datadog {
namespace tracing {

std::variant<Validated<TracerConfig>, Error> validate_config(
    const TracerConfig& config) {
  // TODO: environment variables, validation, and other fun.
  TracerConfig after_env{config};

  if (after_env.defaults.service.empty()) {
    return Error{1337 /* TODO */, "Service name is required."};
  }

  return Validated<TracerConfig>(config, std::move(after_env));
}

}  // namespace tracing
}  // namespace datadog
