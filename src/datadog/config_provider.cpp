#include "config_provider.h"

#include <utility>

#include "config_source.h"

namespace datadog {
namespace tracing {

namespace {

// Append a metadata entry, optionally with a config_id.
void record(std::vector<ConfigMetadata>& entries, ConfigName name,
            std::string value, ConfigMetadata::Origin origin,
            const Optional<std::string>& config_id) {
  ConfigMetadata md(name, std::move(value), origin);
  if (config_id) {
    md.config_id = *config_id;
  }
  entries.emplace_back(std::move(md));
}

}  // namespace

ConfigProvider::ConfigProvider(
    const ConfigSource* fleet, const ConfigSource* env,
    const ConfigSource* local,
    std::unordered_map<ConfigName, std::vector<ConfigMetadata>>* metadata)
    : fleet_(fleet), env_(env), local_(local), metadata_(metadata) {}

std::string ConfigProvider::get_string(ConfigName name, StringView env_key,
                                       const Optional<std::string>& user_value,
                                       std::string default_value) {
  auto& entries = (*metadata_)[name];

  // Record entries lowest-precedence first.  The chosen value is the
  // last one assigned (which corresponds to the highest precedence
  // found).

  // 1. DEFAULT (always recorded)
  record(entries, name, default_value, ConfigMetadata::Origin::DEFAULT,
         nullopt);
  std::string chosen = default_value;

  // 2. local_stable
  if (local_) {
    if (auto v = local_->lookup(env_key)) {
      record(entries, name, *v, local_->origin(), local_->config_id());
      chosen = *v;
    }
  }

  // 3. user/code
  if (user_value) {
    record(entries, name, *user_value, ConfigMetadata::Origin::CODE, nullopt);
    chosen = *user_value;
  }

  // 4. env
  if (env_) {
    if (auto v = env_->lookup(env_key)) {
      record(entries, name, *v, env_->origin(), env_->config_id());
      chosen = *v;
    }
  }

  // 5. fleet_stable (highest)
  if (fleet_) {
    if (auto v = fleet_->lookup(env_key)) {
      record(entries, name, *v, fleet_->origin(), fleet_->config_id());
      chosen = *v;
    }
  }

  return chosen;
}

}  // namespace tracing
}  // namespace datadog
