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

// resolve walks fleet, env, user_value, local, default in precedence
// order.  parse_fn converts a source's raw string into T (returns
// Expected<T>).  stringify renders T as a string for metadata entries
// for the default and user-supplied values (source values use the raw
// string directly).  logger may be nullptr (in which case parse
// failures are silent).
template <typename T, typename ParseFn, typename StringifyFn>
T resolve(ConfigName name, StringView env_key, const Optional<T>& user_value,
          T default_value, const ConfigSource* fleet, const ConfigSource* env,
          const ConfigSource* local,
          std::unordered_map<ConfigName, std::vector<ConfigMetadata>>* metadata,
          ParseFn parse_fn, StringifyFn stringify, Logger* logger) {
  auto& entries = (*metadata)[name];

  auto attempt = [&](const ConfigSource* src) -> Optional<T> {
    if (!src) return nullopt;
    auto raw = src->lookup(env_key);
    if (!raw) return nullopt;
    auto result = parse_fn(*raw);
    if (auto* err = result.if_error()) {
      (void)err;
      if (logger) {
        std::string key_copy{env_key};
        std::string raw_copy = *raw;
        logger->log_error([key_copy, raw_copy](std::ostream& log) {
          log << "Config: invalid value for " << key_copy << ": " << raw_copy
              << "; falling through to lower-precedence source.";
        });
      }
      return nullopt;
    }
    record(entries, name, *raw, src->origin(), src->config_id());
    return *result;
  };

  record(entries, name, stringify(default_value),
         ConfigMetadata::Origin::DEFAULT, nullopt);
  T chosen = default_value;

  if (auto v = attempt(local)) chosen = *v;

  if (user_value) {
    record(entries, name, stringify(*user_value), ConfigMetadata::Origin::CODE,
           nullopt);
    chosen = *user_value;
  }

  if (auto v = attempt(env)) chosen = *v;
  if (auto v = attempt(fleet)) chosen = *v;

  return chosen;
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
  auto parse = [](const std::string& s) -> Expected<std::string> { return s; };
  auto stringify = [](const std::string& s) { return s; };
  return resolve<std::string>(name, env_key, user_value,
                              std::move(default_value), fleet_, env_, local_,
                              metadata_, parse, stringify, nullptr);
}

bool ConfigProvider::get_bool(ConfigName name, StringView env_key,
                              const Optional<bool>& user_value,
                              bool default_value) {
  auto parse = [](const std::string& s) -> Expected<bool> {
    return !falsy(StringView(s));
  };
  auto stringify = [](const bool& b) { return to_string(b); };
  return resolve<bool>(name, env_key, user_value, default_value, fleet_, env_,
                       local_, metadata_, parse, stringify, nullptr);
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
  return resolve<std::size_t>(name, env_key, user_value, default_value, fleet_,
                              env_, local_, metadata_, parse, stringify,
                              &logger);
}

double ConfigProvider::get_double(ConfigName name, StringView env_key,
                                  const Optional<double>& user_value,
                                  double default_value, Logger& logger) {
  auto parse = [](const std::string& s) -> Expected<double> {
    return parse_double(StringView(s));
  };
  auto stringify = [](const double& v) { return to_string(v, 1); };
  return resolve<double>(name, env_key, user_value, default_value, fleet_, env_,
                         local_, metadata_, parse, stringify, &logger);
}

}  // namespace tracing
}  // namespace datadog
