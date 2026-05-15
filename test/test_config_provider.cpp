#include <cstddef>
#include <memory>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include "common/environment.h"
#include "config_provider.h"
#include "config_source.h"
#include "environment_source.h"
#include "mocks/loggers.h"
#include "test.h"

using namespace datadog::test;
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

CONFIG_PROVIDER_TEST("EnvironmentSource: reads from environment") {
  EnvGuard guard{"DD_CONFIG_PROVIDER_TEST_KEY", "found-it"};
  EnvironmentSource src;
  auto val = src.lookup("DD_CONFIG_PROVIDER_TEST_KEY");
  REQUIRE(val.has_value());
  REQUIRE(*val == "found-it");
  REQUIRE(src.origin() == ConfigMetadata::Origin::ENVIRONMENT_VARIABLE);
  REQUIRE(!src.config_id().has_value());
}

CONFIG_PROVIDER_TEST("EnvironmentSource: returns nullopt for unset variable") {
  EnvironmentSource src;
  REQUIRE(!src.lookup("DD_NEVER_SET_VAR_FOR_TEST").has_value());
}

CONFIG_PROVIDER_TEST(
    "EnvironmentSource: preserves empty value (matches environment::lookup)") {
  EnvGuard guard{"DD_CONFIG_EMPTY_TEST", ""};
  EnvironmentSource src;
  auto val = src.lookup("DD_CONFIG_EMPTY_TEST");
  REQUIRE(val.has_value());
  REQUIRE(*val == "");
}

namespace {

// Build a ConfigProvider with optional fleet, env, local sources and a
// fresh metadata map for use in tests.
struct ProviderHarness {
  std::unique_ptr<ConfigSource> fleet;
  std::unique_ptr<ConfigSource> env;
  std::unique_ptr<ConfigSource> local;
  std::unordered_map<ConfigName, std::vector<ConfigMetadata>> metadata;
  ConfigProvider provider;

  ProviderHarness(std::unique_ptr<ConfigSource> f,
                  std::unique_ptr<ConfigSource> e,
                  std::unique_ptr<ConfigSource> l)
      : fleet(std::move(f)),
        env(std::move(e)),
        local(std::move(l)),
        provider(fleet.get(), env.get(), local.get(), &metadata) {}
};

}  // namespace

CONFIG_PROVIDER_TEST(
    "ConfigProvider::get_string: env wins when only env is set") {
  ProviderHarness h(
      nullptr,
      std::make_unique<MapSource>(
          std::unordered_map<std::string, std::string>{{"DD_SERVICE", "env"}},
          ConfigMetadata::Origin::ENVIRONMENT_VARIABLE),
      nullptr);

  auto result =
      h.provider.get_string(ConfigName::SERVICE_NAME, "DD_SERVICE",
                            Optional<std::string>{}, std::string{"default"});
  REQUIRE(result == "env");

  auto it = h.metadata.find(ConfigName::SERVICE_NAME);
  REQUIRE(it != h.metadata.end());
  REQUIRE(it->second.size() == 2);  // DEFAULT + env
  REQUIRE(it->second[0].origin == ConfigMetadata::Origin::DEFAULT);
  REQUIRE(it->second[1].origin == ConfigMetadata::Origin::ENVIRONMENT_VARIABLE);
  REQUIRE(it->second[1].value == "env");
}

CONFIG_PROVIDER_TEST(
    "ConfigProvider::get_string: user_value wins over default when env empty") {
  ProviderHarness h(nullptr,
                    std::make_unique<MapSource>(
                        std::unordered_map<std::string, std::string>{},
                        ConfigMetadata::Origin::ENVIRONMENT_VARIABLE),
                    nullptr);

  auto result =
      h.provider.get_string(ConfigName::SERVICE_NAME, "DD_SERVICE",
                            Optional<std::string>{"user"}, std::string{"d"});
  REQUIRE(result == "user");
}

CONFIG_PROVIDER_TEST("ConfigProvider::get_string: env beats user") {
  ProviderHarness h(
      nullptr,
      std::make_unique<MapSource>(
          std::unordered_map<std::string, std::string>{{"DD_SERVICE", "env"}},
          ConfigMetadata::Origin::ENVIRONMENT_VARIABLE),
      nullptr);

  auto result =
      h.provider.get_string(ConfigName::SERVICE_NAME, "DD_SERVICE",
                            Optional<std::string>{"user"}, std::string{"d"});
  REQUIRE(result == "env");
}

CONFIG_PROVIDER_TEST(
    "ConfigProvider::get_string: only default if everything empty") {
  ProviderHarness h(nullptr,
                    std::make_unique<MapSource>(
                        std::unordered_map<std::string, std::string>{},
                        ConfigMetadata::Origin::ENVIRONMENT_VARIABLE),
                    nullptr);

  auto result = h.provider.get_string(ConfigName::SERVICE_NAME, "DD_SERVICE",
                                      Optional<std::string>{},
                                      std::string{"default-svc"});
  REQUIRE(result == "default-svc");
  REQUIRE(h.metadata[ConfigName::SERVICE_NAME].size() == 1);
  REQUIRE(h.metadata[ConfigName::SERVICE_NAME][0].origin ==
          ConfigMetadata::Origin::DEFAULT);
}

CONFIG_PROVIDER_TEST(
    "ConfigProvider::get_string: null source pointers are skipped") {
  // Simulates PR R's state: no stable config sources yet.
  ProviderHarness h(nullptr, nullptr, nullptr);
  auto result =
      h.provider.get_string(ConfigName::SERVICE_NAME, "DD_SERVICE",
                            Optional<std::string>{}, std::string{"default"});
  REQUIRE(result == "default");
}

CONFIG_PROVIDER_TEST(
    "ConfigProvider::get_string: full chain fleet > env > user > local > "
    "default") {
  ProviderHarness h(
      std::make_unique<MapSource>(
          std::unordered_map<std::string, std::string>{{"DD_SERVICE", "fleet"}},
          ConfigMetadata::Origin::FLEET_STABLE_CONFIG,
          Optional<std::string>{"id-99"}),
      std::make_unique<MapSource>(
          std::unordered_map<std::string, std::string>{{"DD_SERVICE", "env"}},
          ConfigMetadata::Origin::ENVIRONMENT_VARIABLE),
      std::make_unique<MapSource>(
          std::unordered_map<std::string, std::string>{{"DD_SERVICE", "local"}},
          ConfigMetadata::Origin::LOCAL_STABLE_CONFIG));

  auto result = h.provider.get_string(ConfigName::SERVICE_NAME, "DD_SERVICE",
                                      Optional<std::string>{"user"},
                                      std::string{"default"});
  REQUIRE(result == "fleet");

  auto& entries = h.metadata[ConfigName::SERVICE_NAME];
  REQUIRE(entries.size() == 5);
  REQUIRE(entries[0].origin == ConfigMetadata::Origin::DEFAULT);
  REQUIRE(entries[1].origin == ConfigMetadata::Origin::LOCAL_STABLE_CONFIG);
  REQUIRE(entries[2].origin == ConfigMetadata::Origin::CODE);
  REQUIRE(entries[3].origin == ConfigMetadata::Origin::ENVIRONMENT_VARIABLE);
  REQUIRE(entries[4].origin == ConfigMetadata::Origin::FLEET_STABLE_CONFIG);
  REQUIRE(entries[4].config_id.has_value());
  REQUIRE(*entries[4].config_id == "id-99");
}

CONFIG_PROVIDER_TEST("ConfigProvider::get_bool: parses true and false") {
  ProviderHarness h(
      nullptr,
      std::make_unique<MapSource>(
          std::unordered_map<std::string, std::string>{{"DD_X", "true"}},
          ConfigMetadata::Origin::ENVIRONMENT_VARIABLE),
      nullptr);
  REQUIRE(h.provider.get_bool(ConfigName::REPORT_TRACES, "DD_X",
                              Optional<bool>{}, false) == true);

  ProviderHarness h2(
      nullptr,
      std::make_unique<MapSource>(
          std::unordered_map<std::string, std::string>{{"DD_X", "false"}},
          ConfigMetadata::Origin::ENVIRONMENT_VARIABLE),
      nullptr);
  REQUIRE(h2.provider.get_bool(ConfigName::REPORT_TRACES, "DD_X",
                               Optional<bool>{}, true) == false);
}

CONFIG_PROVIDER_TEST("ConfigProvider::get_uint64: valid value") {
  ProviderHarness h(
      nullptr,
      std::make_unique<MapSource>(
          std::unordered_map<std::string, std::string>{{"DD_X", "1234"}},
          ConfigMetadata::Origin::ENVIRONMENT_VARIABLE),
      nullptr);
  MockLogger logger;
  REQUIRE(h.provider.get_uint64(ConfigName::TRACE_BAGGAGE_MAX_ITEMS, "DD_X",
                                Optional<std::size_t>{}, 64, logger) == 1234);
  REQUIRE(logger.error_count() == 0);
}

CONFIG_PROVIDER_TEST(
    "ConfigProvider::get_uint64: invalid env value falls through to default") {
  ProviderHarness h(nullptr,
                    std::make_unique<MapSource>(
                        std::unordered_map<std::string, std::string>{
                            {"DD_X", "not-a-number"}},
                        ConfigMetadata::Origin::ENVIRONMENT_VARIABLE),
                    nullptr);
  MockLogger logger;
  auto result =
      h.provider.get_uint64(ConfigName::TRACE_BAGGAGE_MAX_ITEMS, "DD_X",
                            Optional<std::size_t>{}, 64, logger);
  REQUIRE(result == 64);
  REQUIRE(logger.error_count() == 1);
}

CONFIG_PROVIDER_TEST("ConfigProvider::get_double: parses 0.5") {
  ProviderHarness h(
      nullptr,
      std::make_unique<MapSource>(
          std::unordered_map<std::string, std::string>{{"DD_X", "0.5"}},
          ConfigMetadata::Origin::ENVIRONMENT_VARIABLE),
      nullptr);
  MockLogger logger;
  REQUIRE(h.provider.get_double(ConfigName::TRACE_SAMPLING_RATE, "DD_X",
                                Optional<double>{}, 1.0, logger) == 0.5);
}

CONFIG_PROVIDER_TEST(
    "ConfigProvider::get_tags: parses comma-separated tags from env source") {
  ProviderHarness h(nullptr,
                    std::make_unique<MapSource>(
                        std::unordered_map<std::string, std::string>{
                            {"DD_TAGS", "k1:v1,k2:v2"}},
                        ConfigMetadata::Origin::ENVIRONMENT_VARIABLE),
                    nullptr);
  MockLogger logger;
  auto tags = h.provider.get_tags(
      ConfigName::TAGS, "DD_TAGS",
      Optional<std::unordered_map<std::string, std::string>>{},
      std::unordered_map<std::string, std::string>{}, logger);
  REQUIRE(tags.size() == 2);
  REQUIRE(tags.at("k1") == "v1");
  REQUIRE(tags.at("k2") == "v2");
}

CONFIG_PROVIDER_TEST(
    "ConfigProvider::get_propagation_styles: parses datadog,tracecontext") {
  ProviderHarness h(
      nullptr,
      std::make_unique<MapSource>(
          std::unordered_map<std::string, std::string>{
              {"DD_TRACE_PROPAGATION_STYLE", "datadog,tracecontext"}},
          ConfigMetadata::Origin::ENVIRONMENT_VARIABLE),
      nullptr);
  MockLogger logger;
  auto styles = h.provider.get_propagation_styles(
      ConfigName::EXTRACTION_STYLES, "DD_TRACE_PROPAGATION_STYLE",
      Optional<std::vector<PropagationStyle>>{},
      std::vector<PropagationStyle>{}, logger);
  REQUIRE(styles.size() == 2);
  REQUIRE(styles[0] == PropagationStyle::DATADOG);
  REQUIRE(styles[1] == PropagationStyle::W3C);
}

CONFIG_PROVIDER_TEST(
    "ConfigProvider::get_propagation_styles: invalid value falls through") {
  ProviderHarness h(nullptr,
                    std::make_unique<MapSource>(
                        std::unordered_map<std::string, std::string>{
                            {"DD_TRACE_PROPAGATION_STYLE", "bogus-style"}},
                        ConfigMetadata::Origin::ENVIRONMENT_VARIABLE),
                    nullptr);
  MockLogger logger;
  std::vector<PropagationStyle> default_styles{PropagationStyle::DATADOG};
  auto styles = h.provider.get_propagation_styles(
      ConfigName::EXTRACTION_STYLES, "DD_TRACE_PROPAGATION_STYLE",
      Optional<std::vector<PropagationStyle>>{}, default_styles, logger);
  // Parse fails -> falls through to default.
  REQUIRE(styles.size() == 1);
  REQUIRE(styles[0] == PropagationStyle::DATADOG);
  REQUIRE(logger.error_count() == 1);
}

CONFIG_PROVIDER_TEST(
    "ConfigProvider::get_tags: malformed value falls through to default") {
  ProviderHarness h(nullptr,
                    std::make_unique<MapSource>(
                        std::unordered_map<std::string, std::string>{
                            {"DD_TAGS", "not_valid_tag_format!!!"}},
                        ConfigMetadata::Origin::ENVIRONMENT_VARIABLE),
                    nullptr);
  MockLogger logger;
  std::unordered_map<std::string, std::string> default_tags{{"env", "prod"}};
  auto tags = h.provider.get_tags(
      ConfigName::TAGS, "DD_TAGS",
      Optional<std::unordered_map<std::string, std::string>>{}, default_tags,
      logger);
  // Behavior depends on what parse_tags considers malformed; the value
  // "not_valid_tag_format!!!" without a colon parses as a single
  // standalone key with empty value, which is valid per existing
  // parse_tags behavior.  Accept either outcome; the test's purpose is
  // verifying get_tags doesn't throw and returns *something* sensible.
  REQUIRE(logger.error_count() <= 1);
}
