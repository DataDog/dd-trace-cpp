#pragma once

// `ConfigProvider` is a source-list resolver.  It owns references to
// `ConfigSource` instances, walks them in precedence order on each
// lookup, records each non-empty source's value into a `ConfigMetadata`
// map for telemetry, and returns the highest-precedence value.
//
// Precedence (highest to lowest):
//   fleet_stable > env > user/code > local_stable > default
//
// User-supplied values come from typed struct fields and plumb in
// through the accessor's `user_value` parameter rather than as a
// separate source.  Defaults plumb in through the accessor's
// `default_value` parameter.

#include <datadog/config.h>
#include <datadog/logger.h>
#include <datadog/optional.h>
#include <datadog/propagation_style.h>
#include <datadog/string_view.h>

#include <cstddef>
#include <initializer_list>
#include <ostream>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include "config_source.h"

namespace datadog {
namespace tracing {

class ConfigProvider {
  const ConfigSource* fleet_;
  const ConfigSource* env_;
  const ConfigSource* local_;
  std::unordered_map<ConfigName, std::vector<ConfigMetadata>>* metadata_;

 public:
  // Any source pointer may be nullptr (that source is skipped).  The
  // metadata map must outlive the provider.
  ConfigProvider(
      const ConfigSource* fleet, const ConfigSource* env,
      const ConfigSource* local,
      std::unordered_map<ConfigName, std::vector<ConfigMetadata>>* metadata);

  // String accessor.  Walks fleet, env, user_value, local, default in
  // precedence order; records each non-empty value into metadata.
  std::string get_string(ConfigName name, StringView env_key,
                         const Optional<std::string>& user_value,
                         std::string default_value);

  // Boolean accessor.  Matches the env-var convention: any non-falsy
  // string is treated as true.
  bool get_bool(ConfigName name, StringView env_key,
                const Optional<bool>& user_value, bool default_value);

  // Unsigned-integer accessor.  Logs an error and falls through to the
  // next-lower-precedence source on parse failure.
  std::size_t get_uint64(ConfigName name, StringView env_key,
                         const Optional<std::size_t>& user_value,
                         std::size_t default_value, Logger& logger);

  // Floating-point accessor.  Logs and falls through on parse failure.
  double get_double(ConfigName name, StringView env_key,
                    const Optional<double>& user_value, double default_value,
                    Logger& logger);

  // Tag-map accessor.  Parses comma-separated `key:value` pairs (the
  // DD_TAGS convention).
  std::unordered_map<std::string, std::string> get_tags(
      ConfigName name, StringView env_key,
      const Optional<std::unordered_map<std::string, std::string>>& user_value,
      std::unordered_map<std::string, std::string> default_value,
      Logger& logger);

  // Propagation-style list accessor.  Parses a comma-or-space-separated
  // list of style names.
  std::vector<PropagationStyle> get_propagation_styles(
      ConfigName name, StringView env_key,
      const Optional<std::vector<PropagationStyle>>& user_value,
      std::vector<PropagationStyle> default_value, Logger& logger);

  // Propagation-style list accessor with env-key fallback chain.  Tries
  // each `env_keys` entry in order; uses the first one where a source
  // contributes a value.  Used for DD_TRACE_PROPAGATION_STYLE_EXTRACT >
  // DD_PROPAGATION_STYLE_EXTRACT > DD_TRACE_PROPAGATION_STYLE and the
  // symmetric injection chain.
  std::vector<PropagationStyle> get_propagation_styles_with_aliases(
      ConfigName name, std::initializer_list<StringView> env_keys,
      const Optional<std::vector<PropagationStyle>>& user_value,
      std::vector<PropagationStyle> default_value, Logger& logger);

  // Generic accessor: walks the source list in precedence order with a
  // caller-supplied parse function (string -> Expected<T>) and a
  // stringify function (T -> string) used for default/user metadata
  // entries.  On parse failure for a source, the error is logged
  // (unless `logger` is nullptr) and the resolver falls through to the
  // next-lower-precedence source.  Use this for types that have no
  // dedicated accessor (e.g., span/trace sampling rules).
  template <typename T, typename ParseFn, typename StringifyFn>
  T get(ConfigName name, StringView env_key, const Optional<T>& user_value,
        T default_value, ParseFn parse_fn, StringifyFn stringify,
        Logger* logger);

 private:
  // Append a metadata entry with optional config_id.  Static helper used
  // by the generic resolver above.
  static void record_entry(std::vector<ConfigMetadata>& entries,
                           ConfigName name, std::string value,
                           ConfigMetadata::Origin origin,
                           const Optional<std::string>& config_id);
};

template <typename T, typename ParseFn, typename StringifyFn>
T ConfigProvider::get(ConfigName name, StringView env_key,
                      const Optional<T>& user_value, T default_value,
                      ParseFn parse_fn, StringifyFn stringify, Logger* logger) {
  auto& entries = (*metadata_)[name];

  auto attempt = [&](const ConfigSource* src) -> Optional<T> {
    if (!src) return nullopt;
    auto raw = src->lookup(env_key);
    if (!raw) return nullopt;
    auto result = parse_fn(*raw);
    if (auto* err = result.if_error()) {
      if (logger) {
        std::string key_copy{env_key};
        std::string raw_copy = *raw;
        std::string err_msg = err->message;
        logger->log_error([key_copy, raw_copy, err_msg](std::ostream& log) {
          log << "Config: invalid value for " << key_copy << ": " << raw_copy
              << " (" << err_msg
              << "); falling through to lower-precedence source.";
        });
      }
      return nullopt;
    }
    record_entry(entries, name, *raw, src->origin(), src->config_id());
    return *result;
  };

  record_entry(entries, name, stringify(default_value),
               ConfigMetadata::Origin::DEFAULT, nullopt);
  T chosen = default_value;

  if (auto v = attempt(local_)) chosen = *v;
  if (user_value) {
    record_entry(entries, name, stringify(*user_value),
                 ConfigMetadata::Origin::CODE, nullopt);
    chosen = *user_value;
  }
  if (auto v = attempt(env_)) chosen = *v;
  if (auto v = attempt(fleet_)) chosen = *v;

  return chosen;
}

}  // namespace tracing
}  // namespace datadog
