#pragma once

#include "config_source.h"

namespace datadog {
namespace tracing {

// `ConfigSource` backed by `environment::lookup`.
class EnvironmentSource : public ConfigSource {
 public:
  Optional<std::string> lookup(StringView key) const override;
  ConfigMetadata::Origin origin() const override;
};

}  // namespace tracing
}  // namespace datadog
