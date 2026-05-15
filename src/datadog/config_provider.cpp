#include "config_provider.h"

#include <datadog/expected.h>
#include <datadog/logger.h>

#include <ostream>
#include <utility>

#include "config_source.h"
#include "parse_util.h"
#include "string_util.h"

namespace datadog {
namespace tracing {

void ConfigProvider::record_entry(std::vector<ConfigMetadata>& entries,
                                  ConfigName name, std::string value,
                                  ConfigMetadata::Origin origin,
                                  const Optional<std::string>& config_id) {
  ConfigMetadata md(name, std::move(value), origin);
  if (config_id) {
    md.config_id = *config_id;
  }
  entries.emplace_back(std::move(md));
}

ConfigProvider::ConfigProvider(
    const ConfigSource* fleet, const ConfigSource* env,
    const ConfigSource* local,
    std::unordered_map<ConfigName, std::vector<ConfigMetadata>>* metadata)
    : fleet_(fleet), env_(env), local_(local), metadata_(metadata) {}

std::string ConfigProvider::get_string(ConfigName name, StringView env_key,
                                       const Optional<std::string>& user_value,
                                       std::string default_value) {
  auto parse = [](const std::string& s) -> Expected<std::string> { return s; };
  auto stringify = [](const std::string& s) { return s; };
  return get<std::string>(name, env_key, user_value, std::move(default_value),
                          parse, stringify, nullptr);
}

bool ConfigProvider::get_bool(ConfigName name, StringView env_key,
                              const Optional<bool>& user_value,
                              bool default_value) {
  auto parse = [](const std::string& s) -> Expected<bool> {
    return !falsy(StringView(s));
  };
  auto stringify = [](const bool& b) { return to_string(b); };
  return get<bool>(name, env_key, user_value, default_value, parse, stringify,
                   nullptr);
}

std::size_t ConfigProvider::get_uint64(ConfigName name, StringView env_key,
                                       const Optional<std::size_t>& user_value,
                                       std::size_t default_value,
                                       Logger& logger) {
  auto parse = [](const std::string& s) -> Expected<std::size_t> {
    auto r = parse_uint64(StringView(s), 10);
    if (auto* err = r.if_error()) return *err;
    return static_cast<std::size_t>(*r);
  };
  auto stringify = [](const std::size_t& v) { return std::to_string(v); };
  return get<std::size_t>(name, env_key, user_value, default_value, parse,
                          stringify, &logger);
}

double ConfigProvider::get_double(ConfigName name, StringView env_key,
                                  const Optional<double>& user_value,
                                  double default_value, Logger& logger) {
  auto parse = [](const std::string& s) -> Expected<double> {
    return parse_double(StringView(s));
  };
  auto stringify = [](const double& v) { return to_string(v, 1); };
  return get<double>(name, env_key, user_value, default_value, parse, stringify,
                     &logger);
}

std::unordered_map<std::string, std::string> ConfigProvider::get_tags(
    ConfigName name, StringView env_key,
    const Optional<std::unordered_map<std::string, std::string>>& user_value,
    std::unordered_map<std::string, std::string> default_value,
    Logger& logger) {
  using TagMap = std::unordered_map<std::string, std::string>;
  auto parse = [](const std::string& s) -> Expected<TagMap> {
    return parse_tags(StringView(s));
  };
  auto stringify = [](const TagMap& m) { return join_tags(m); };
  return get<TagMap>(name, env_key, user_value, std::move(default_value), parse,
                     stringify, &logger);
}

std::vector<PropagationStyle> ConfigProvider::get_propagation_styles(
    ConfigName name, StringView env_key,
    const Optional<std::vector<PropagationStyle>>& user_value,
    std::vector<PropagationStyle> default_value, Logger& logger) {
  using StyleList = std::vector<PropagationStyle>;
  auto parse = [](const std::string& s) -> Expected<StyleList> {
    return parse_propagation_styles(StringView(s));
  };
  auto stringify = [](const StyleList& s) {
    return join_propagation_styles(s);
  };
  return get<StyleList>(name, env_key, user_value, std::move(default_value),
                        parse, stringify, &logger);
}

std::vector<PropagationStyle>
ConfigProvider::get_propagation_styles_with_aliases(
    ConfigName name, std::initializer_list<StringView> env_keys,
    const Optional<std::vector<PropagationStyle>>& user_value,
    std::vector<PropagationStyle> default_value, Logger& logger) {
  // Try each env_key in order.  Use the first that any source supplies.
  // Behavior detail: each call records its own metadata entries; only
  // the call that produced a non-default winner survives in the final
  // map (the others contribute their DEFAULT entry, then overwritten).
  // To avoid metadata noise, peek the env source first and skip empty
  // keys before delegating.
  for (auto env_key : env_keys) {
    bool any_source_has_value = false;
    if (fleet_ && fleet_->lookup(env_key))
      any_source_has_value = true;
    else if (env_ && env_->lookup(env_key))
      any_source_has_value = true;
    else if (local_ && local_->lookup(env_key))
      any_source_has_value = true;
    if (any_source_has_value) {
      return get_propagation_styles(name, env_key, user_value,
                                    std::move(default_value), logger);
    }
  }
  // No source supplied any key.  Fall through to user_value or default
  // via the regular accessor using the first (canonical) key.  This
  // also records a DEFAULT entry for telemetry.
  return get_propagation_styles(name, *env_keys.begin(), user_value,
                                std::move(default_value), logger);
}

}  // namespace tracing
}  // namespace datadog
