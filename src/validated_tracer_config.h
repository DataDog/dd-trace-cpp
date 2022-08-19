#pragma once

#include <variant>

#include "tracer_config.h"

namespace datadog {
namespace tracing {

class Error;

class ValidatedTracerConfig {
  // declared in `tracer.h`
  friend std::variant<ValidatedTracerConfig, Error> validate_config(
      const TracerConfig&);

  TracerConfig before_env_;
  TracerConfig after_env_;

  ValidatedTracerConfig(const TracerConfig& before_env,
                        TracerConfig&& after_env);

 public:
  ValidatedTracerConfig() = delete;

  const TracerConfig& operator*() const { return after_env_; }
  TracerConfig& operator*() { return after_env_; }
  const TracerConfig* operator->() const { return &after_env_; }
  TracerConfig* operator->() { return &after_env_; }
};

}  // namespace tracing
}  // namespace datadog
