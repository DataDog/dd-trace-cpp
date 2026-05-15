#pragma once

// This component provides the `ConfigSource` interface, the building
// block of `ConfigProvider`.  Each `ConfigSource` represents one origin
// from which configuration values can be read (environment variables,
// fleet stable config file, local stable config file, etc.).
//
// Sources are looked up by `ConfigProvider` in a fixed precedence order.
// Each source returns either a string value or `nullopt`; conversion to
// typed values (bool, int, etc.) is done by `ConfigProvider`.

#include <datadog/config.h>
#include <datadog/optional.h>
#include <datadog/string_view.h>

#include <string>

namespace datadog {
namespace tracing {

class ConfigSource {
 public:
  virtual ~ConfigSource() = default;

  // Return the raw string value for `key`, or `nullopt` if this source
  // has no value for the key.
  virtual Optional<std::string> lookup(StringView key) const = 0;

  // The telemetry origin associated with this source.
  virtual ConfigMetadata::Origin origin() const = 0;

  // Optional config identifier (only fleet stable config has one).
  virtual Optional<std::string> config_id() const { return nullopt; }
};

}  // namespace tracing
}  // namespace datadog
