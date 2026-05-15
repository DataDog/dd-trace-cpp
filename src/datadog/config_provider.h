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
#include <datadog/string_view.h>

#include <string>
#include <unordered_map>
#include <vector>

namespace datadog {
namespace tracing {

class ConfigSource;

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
};

}  // namespace tracing
}  // namespace datadog
