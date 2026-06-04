#include <datadog/runtime_id.h>
#include <datadog/tracer.h>
#include <datadog/tracer_config.h>
#include <datadog/version.h>

#include <filesystem>
#include <map>
#include <string>
#include <unordered_map>

#include "mocks/collectors.h"
#include "mocks/loggers.h"
#include "otel_process_ctx.h"
#include "platform_util.h"
#include "string_util.h"
#include "test.h"

namespace fs = std::filesystem;
using namespace datadog::tracing;

#define OTEL_CTX_TEST(x) TEST_CASE(x, "[otel_process_ctx]")

namespace {

std::map<std::string, std::string> to_map(const char** kv) {
  std::map<std::string, std::string> out;
  if (kv == nullptr) return out;
  for (std::size_t i = 0; kv[i] != nullptr && kv[i + 1] != nullptr; i += 2) {
    out.emplace(kv[i], kv[i + 1]);
  }
  return out;
}

}  // namespace

OTEL_CTX_TEST("Tracer construction publishes OTel process context") {
#ifndef __linux__
  SUCCEED("OpenTelemetry process context is Linux-only");
  return;
#endif

  std::string expected_container_id;
  if (auto id = container::get_id()) {
    expected_container_id = id->value;
  }
  const RuntimeID expected_runtime_id = RuntimeID::generate();

  std::unordered_map<std::string, std::string> expected_process_tags = {
      {"custom.tag", "custom-value"},
  };
  expected_process_tags.emplace("entrypoint.name", get_process_name());
  expected_process_tags.emplace("entrypoint.type", "executable");
  expected_process_tags.emplace("entrypoint.workdir",
                                fs::current_path().filename().string());
  if (auto path = get_process_path()) {
    expected_process_tags.emplace("entrypoint.basedir",
                                  path->parent_path().filename().string());
  }

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
    CHECK(std::string(data.service_instance_id) == expected_runtime_id.string());
    CHECK(std::string(data.telemetry_sdk_language) == "cpp");
    CHECK(std::string(data.telemetry_sdk_name) == "dd-trace-cpp");
    CHECK(std::string(data.telemetry_sdk_version) == tracer_version);

    const std::map<std::string, std::string> expected_resource = {
        {"host.name", get_hostname()},
        {"container.id", expected_container_id},
    };
    CHECK(to_map(data.resource_attributes) == expected_resource);

    const std::map<std::string, std::string> expected_extra = {
        {"datadog.process_tags", join_tags(expected_process_tags)},
    };
    CHECK(to_map(data.extra_attributes) == expected_extra);

    REQUIRE(otel_process_ctx_read_drop(&read_result));
  }

  auto post_read = otel_process_ctx_read();
  CHECK_FALSE(post_read.success);
  if (post_read.success) {
    otel_process_ctx_read_drop(&post_read);
  }
}
