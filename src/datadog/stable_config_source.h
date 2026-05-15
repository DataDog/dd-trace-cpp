#pragma once

// `ConfigSource` adapters that wrap a parsed `StableConfig` so it can
// participate in the `ConfigProvider` source list alongside the
// environment and any future sources.

#include <datadog/config.h>
#include <datadog/optional.h>
#include <datadog/string_view.h>

#include <string>

#include "config_source.h"
#include "stable_config.h"

namespace datadog {
namespace tracing {

class LocalStableConfigSource : public ConfigSource {
  const StableConfig* cfg_;

 public:
  explicit LocalStableConfigSource(const StableConfig& cfg) : cfg_(&cfg) {}

  Optional<std::string> lookup(StringView key) const override {
    return cfg_->lookup(std::string{key});
  }

  ConfigMetadata::Origin origin() const override {
    return ConfigMetadata::Origin::LOCAL_STABLE_CONFIG;
  }
};

class FleetStableConfigSource : public ConfigSource {
  const StableConfig* cfg_;

 public:
  explicit FleetStableConfigSource(const StableConfig& cfg) : cfg_(&cfg) {}

  Optional<std::string> lookup(StringView key) const override {
    return cfg_->lookup(std::string{key});
  }

  ConfigMetadata::Origin origin() const override {
    return ConfigMetadata::Origin::FLEET_STABLE_CONFIG;
  }

  Optional<std::string> config_id() const override { return cfg_->config_id; }
};

}  // namespace tracing
}  // namespace datadog
