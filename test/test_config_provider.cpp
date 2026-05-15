#include <cstdlib>
#include <string>
#include <unordered_map>
#include <utility>

#include "config_source.h"
#include "environment_source.h"
#include "test.h"

using namespace datadog::tracing;

namespace {

// Minimal in-memory ConfigSource for tests.  Used in this file and the
// rest of the ConfigProvider test suite.
class MapSource : public ConfigSource {
  std::unordered_map<std::string, std::string> values_;
  ConfigMetadata::Origin origin_;
  Optional<std::string> config_id_;

 public:
  MapSource(std::unordered_map<std::string, std::string> values,
            ConfigMetadata::Origin origin,
            Optional<std::string> config_id = nullopt)
      : values_(std::move(values)),
        origin_(origin),
        config_id_(std::move(config_id)) {}

  Optional<std::string> lookup(StringView key) const override {
    auto it = values_.find(std::string(key));
    if (it == values_.end()) return nullopt;
    return it->second;
  }

  ConfigMetadata::Origin origin() const override { return origin_; }
  Optional<std::string> config_id() const override { return config_id_; }
};

}  // namespace

#define CONFIG_PROVIDER_TEST(x) TEST_CASE(x, "[config_provider]")

CONFIG_PROVIDER_TEST("ConfigSource: MapSource returns stored value") {
  MapSource src({{"DD_SERVICE", "my-service"}},
                ConfigMetadata::Origin::ENVIRONMENT_VARIABLE);
  auto val = src.lookup("DD_SERVICE");
  REQUIRE(val.has_value());
  REQUIRE(*val == "my-service");
  REQUIRE(src.origin() == ConfigMetadata::Origin::ENVIRONMENT_VARIABLE);
  REQUIRE(!src.config_id().has_value());
}

CONFIG_PROVIDER_TEST(
    "ConfigSource: MapSource returns nullopt for missing key") {
  MapSource src({}, ConfigMetadata::Origin::ENVIRONMENT_VARIABLE);
  REQUIRE(!src.lookup("DD_SERVICE").has_value());
}

CONFIG_PROVIDER_TEST("EnvironmentSource: reads from getenv") {
  ::setenv("DD_CONFIG_PROVIDER_TEST_KEY", "found-it", 1);
  EnvironmentSource src;
  auto val = src.lookup("DD_CONFIG_PROVIDER_TEST_KEY");
  ::unsetenv("DD_CONFIG_PROVIDER_TEST_KEY");
  REQUIRE(val.has_value());
  REQUIRE(*val == "found-it");
  REQUIRE(src.origin() == ConfigMetadata::Origin::ENVIRONMENT_VARIABLE);
  REQUIRE(!src.config_id().has_value());
}

CONFIG_PROVIDER_TEST("EnvironmentSource: returns nullopt for unset variable") {
  ::unsetenv("DD_NEVER_SET_VAR_FOR_TEST");
  EnvironmentSource src;
  REQUIRE(!src.lookup("DD_NEVER_SET_VAR_FOR_TEST").has_value());
}

CONFIG_PROVIDER_TEST("EnvironmentSource: returns nullopt for empty variable") {
  ::setenv("DD_CONFIG_EMPTY_TEST", "", 1);
  EnvironmentSource src;
  auto val = src.lookup("DD_CONFIG_EMPTY_TEST");
  ::unsetenv("DD_CONFIG_EMPTY_TEST");
  REQUIRE(!val.has_value());
}
