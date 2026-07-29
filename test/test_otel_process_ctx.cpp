#include <datadog/tracer.h>

#include "common/ctx_sharing_helpers.h"
#include "mocks/collectors.h"
#include "mocks/loggers.h"
#include "otel_process_ctx.h"
#include "platform_util.h"
#include "test.h"

using namespace datadog::tracing;

#define OTEL_CTX_TEST(x) TEST_CASE(x, "[otel_process_ctx]")

namespace {

std::map<std::string, std::string> to_map(const char** key_value_array) {
  std::map<std::string, std::string> out;
  if (key_value_array == nullptr) return out;
  for (std::size_t index = 0; key_value_array[index] != nullptr; index += 2) {
    REQUIRE(key_value_array[index + 1] != nullptr);
    out.emplace(key_value_array[index], key_value_array[index + 1]);
  }
  return out;
}

#ifdef __linux__
std::unique_ptr<Tracer> make_tracer(
    const RuntimeID& runtime_id, const std::string& service = "otel-ctx-svc",
    std::shared_ptr<Logger> logger = std::make_shared<MockLogger>()) {
  TracerConfig config;
  config.service = service;
  config.runtime_id = runtime_id;
  config.collector = std::make_shared<MockCollector>();
  config.logger = std::move(logger);

  auto finalized = finalize_config(config);
  REQUIRE(finalized);
  return std::make_unique<Tracer>(*finalized);
}

Optional<std::string> current_instance_id() {
  auto read_result = otel_process_ctx_read();
  if (!read_result.success) return nullopt;
  std::string instance_id(read_result.data.service_instance_id);
  otel_process_ctx_read_drop(&read_result);
  return instance_id;
}
#endif

}  // namespace

OTEL_CTX_TEST("Tracer construction publishes OTel process context") {
#ifndef __linux__
  SUCCEED("OpenTelemetry process context is Linux-only");
#else
  REQUIRE(current_instance_id() == nullopt);  // no context leaked from earlier

  std::string expected_container_id;
  if (auto id = container::get_id()) {
    expected_container_id = id->value;
  }
  const RuntimeID expected_runtime_id = RuntimeID::generate();

  TracerConfig config;
  config.service = "otel-ctx-svc";
  config.environment = "otel-ctx-env";
  config.version = "1.2.3";
  config.runtime_id = expected_runtime_id;
  config.report_hostname = true;
  config.process_tags = {{"custom.tag", "custom-value"}};
  config.collector = std::make_shared<MockCollector>();
  config.logger = std::make_shared<MockLogger>();

  auto finalized = finalize_config(config);
  REQUIRE(finalized);

  {
    Tracer tracer{*finalized};

    auto read_result = otel_process_ctx_read();
    REQUIRE(read_result.success);
    const auto& data = read_result.data;

    CHECK(std::string(data.service_name) == "otel-ctx-svc");
    CHECK(std::string(data.deployment_environment_name) == "otel-ctx-env");
    CHECK(std::string(data.service_version) == "1.2.3");
    CHECK(std::string(data.service_instance_id) ==
          expected_runtime_id.string());
    CHECK(std::string(data.telemetry_sdk_language) == "cpp");
    CHECK(std::string(data.telemetry_sdk_name) == "dd-trace-cpp");
    CHECK(std::string(data.telemetry_sdk_version) == tracer_version);

    const std::map<std::string, std::string> expected_resource = {
        {"host.name", get_hostname()},
        {"container.id", expected_container_id},
    };
    CHECK(to_map(data.resource_attributes) == expected_resource);

    const auto extra = to_map(data.extra_attributes);
    REQUIRE(extra.size() == 1);
    REQUIRE(extra.count("datadog.process_tags") == 1);
    CHECK(datadog::test::parse_joined_tags(extra.at("datadog.process_tags")) ==
          datadog::test::expected_published_process_tags(config.process_tags));

    REQUIRE(otel_process_ctx_read_drop(&read_result));
  }

  auto post_read = otel_process_ctx_read();
  CHECK_FALSE(post_read.success);
  if (post_read.success) {
    otel_process_ctx_read_drop(&post_read);
  }
#endif
}

OTEL_CTX_TEST("host.name is omitted when report_hostname is false") {
#ifndef __linux__
  SUCCEED("OpenTelemetry process context is Linux-only");
#else
  REQUIRE(current_instance_id() == nullopt);  // no context leaked from earlier

  TracerConfig config;
  config.service = "otel-ctx-svc";
  config.report_hostname = false;
  config.collector = std::make_shared<MockCollector>();
  config.logger = std::make_shared<MockLogger>();

  auto finalized = finalize_config(config);
  REQUIRE(finalized);

  Tracer tracer{*finalized};
  auto read_result = otel_process_ctx_read();
  REQUIRE(read_result.success);

  // Sanity check that context is not empty
  CHECK(std::string(read_result.data.service_name) == "otel-ctx-svc");

  const auto resource = to_map(read_result.data.resource_attributes);
  CHECK(resource.count("host.name") == 0);

  REQUIRE(otel_process_ctx_read_drop(&read_result));
#endif
}

OTEL_CTX_TEST(
    "service_instance_id is omitted while multiple Tracers are alive") {
#ifndef __linux__
  SUCCEED("OpenTelemetry process context is Linux-only");
#else
  REQUIRE(current_instance_id() == nullopt);  // no context leaked from earlier

  const RuntimeID runtime_id_1 = RuntimeID::generate();
  const RuntimeID runtime_id_2 = RuntimeID::generate();

  auto tracer1 = make_tracer(runtime_id_1);
  CHECK(current_instance_id() == runtime_id_1.string());

  auto tracer2 = make_tracer(runtime_id_2);
  CHECK(current_instance_id() == "");

  tracer2.reset();
  CHECK(current_instance_id() == runtime_id_1.string());

  tracer1.reset();
  CHECK(current_instance_id() == nullopt);
#endif
}

OTEL_CTX_TEST("service_instance_id returns only when a single Tracer remains") {
#ifndef __linux__
  SUCCEED("OpenTelemetry process context is Linux-only");
#else
  REQUIRE(current_instance_id() == nullopt);  // no context leaked from earlier

  const RuntimeID runtime_id_1 = RuntimeID::generate();
  const RuntimeID runtime_id_2 = RuntimeID::generate();
  const RuntimeID runtime_id_3 = RuntimeID::generate();

  auto tracer1 = make_tracer(runtime_id_1);
  auto tracer2 = make_tracer(runtime_id_2);
  auto tracer3 = make_tracer(runtime_id_3);

  CHECK(current_instance_id() == "");

  tracer1.reset();
  CHECK(current_instance_id() == "");

  tracer2.reset();
  CHECK(current_instance_id() == runtime_id_3.string());
#endif
}

OTEL_CTX_TEST(
    "a later Tracer with different config fields is logged and ignored") {
#ifndef __linux__
  SUCCEED("OpenTelemetry process context is Linux-only");
#else
  REQUIRE(current_instance_id() == nullopt);  // no context leaked from earlier

  const RuntimeID runtime_id_1 = RuntimeID::generate();
  const RuntimeID runtime_id_2 = RuntimeID::generate();

  auto tracer1 = make_tracer(runtime_id_1, "svc-one");

  auto logger2 = std::make_shared<MockLogger>();
  auto tracer2 = make_tracer(runtime_id_2, "svc-two", logger2);

  REQUIRE(logger2->error_count() == 1);
  CHECK_THAT(std::get<std::string>(logger2->entries.back().payload),
             Catch::Matchers::Contains("fields differ between coexisting"));
  {
    auto read_result = otel_process_ctx_read();
    REQUIRE(read_result.success);
    CHECK(std::string(read_result.data.service_name) == "svc-one");
    REQUIRE(otel_process_ctx_read_drop(&read_result));
  }
#endif
}
