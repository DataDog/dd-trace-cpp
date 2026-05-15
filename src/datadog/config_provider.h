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
#include <datadog/optional.h>
#include <datadog/propagation_style.h>
#include <datadog/string_view.h>

#include <cstddef>
#include <string>
#include <unordered_map>
#include <vector>

namespace datadog {
namespace tracing {

class ConfigSource;
class Logger;

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
};

}  // namespace tracing
}  // namespace datadog
