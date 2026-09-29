#include <datadog/stable_config_loader.h>

#include <cstdlib>
#include <memory>
#include <string>

using namespace datadog::tracing;

namespace {

struct TestHTTPClient : HTTPClient {
  Expected<void> post(const URL&, HeadersSetter, std::string, ResponseHandler,
                      ErrorHandler,
                      std::chrono::steady_clock::time_point) override {
    return {};
  }

  void drain(std::chrono::steady_clock::time_point) override {}

  std::string config() const override { return "{}"; }
};

}  // namespace

int main() {
  const char* runfiles = std::getenv("TEST_SRCDIR");
  const char* workspace = std::getenv("TEST_WORKSPACE");
  if (!runfiles || !workspace) return 1;
  const std::string base =
      std::string(runfiles) + "/" + workspace + "/test/fixtures/stable_config/";

  TracerConfig config;
  config.agent.http_client = std::make_shared<TestHTTPClient>();
  auto finalized = finalize_config_with_stable_config(
      config, "cpp", base + "local.yaml", base + "fleet.yaml");
  if (!finalized) return 2;
  if (finalized->defaults.service != "fleet-service") return 3;
  if (finalized->extraction_styles.size() != 1 ||
      finalized->extraction_styles.front() != PropagationStyle::B3) {
    return 4;
  }
  return 0;
}
