// Executable that calls exit() while a Tracer, held by a static wrapper, is
// still alive.

#include <datadog/collector.h>
#include <datadog/tracer.h>

namespace dd = datadog::tracing;

namespace {

class NullCollector : public dd::Collector {
 public:
  dd::Expected<void> send(std::vector<std::unique_ptr<dd::SpanData>>&&,
                          const std::shared_ptr<dd::TraceSampler>&) override {
    return dd::nullopt;
  }

  std::string config() const override { return R"({"type": "NullCollector"})"; }
};

// Created empty before main(), so before the library statics.
std::optional<dd::Tracer> tracer;

}  // namespace

int main() {
  dd::TracerConfig config;
  config.collector = std::make_shared<NullCollector>();
  config.telemetry.enabled = false;
  const dd::Expected<dd::FinalizedTracerConfig> finalized_config =
      dd::finalize_config(config);
  if (!finalized_config) {
    return EXIT_FAILURE;
  }
  tracer.emplace(*finalized_config);
  std::exit(EXIT_SUCCESS);
}
