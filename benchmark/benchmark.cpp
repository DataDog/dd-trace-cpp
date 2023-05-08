#include <benchmark/benchmark.h>

#include <datadog/collector.h>
#include <datadog/json.hpp>
#include <datadog/logger.h>
#include <datadog/span_data.h>
#include <datadog/tracer.h>
#include <datadog/tracer_config.h>

#include <iostream> // TODO: no
#include <memory>

#include "hasher.h"

namespace {

namespace dd = datadog::tracing;

struct NullLogger : public dd::Logger {
  void log_error(const LogFunc&) override {}
  void log_startup(const LogFunc&) override {}
  void log_error(const dd::Error&) override {}
  void log_error(dd::StringView) override {}
};

struct SerializingCollector : public dd::Collector {
  dd::Expected<void> send(
      std::vector<std::unique_ptr<dd::SpanData>>&& spans,
      const std::shared_ptr<dd::TraceSampler>& /*response_handler*/) override {
    std::cout << "Received " << spans.size() << " spans.\n"; // TODO: no
    std::string buffer;
    return dd::msgpack_encode(buffer, spans);
  }

  nlohmann::json config_json() const override {
    return nlohmann::json::object({
        {"type", "SerializingCollector"}
    });
  }
};

void BM_Nothing(benchmark::State& state) {
  for (auto _ : state) {
  }
}
BENCHMARK(BM_Nothing);

void BM_StringCopy(benchmark::State& state) {
  std::string x = "hello";
  for (auto _ : state) {
      std::string copy{x};
  }
}
BENCHMARK(BM_StringCopy);

void BM_TraceTinyCCSource(benchmark::State& state) {
  for (auto _ : state) {
    dd::TracerConfig config;
    config.defaults.service = "benchmark";
    config.logger = std::make_shared<NullLogger>();
    config.collector = std::make_shared<SerializingCollector>();
    const auto valid_config = dd::finalize_config(config);
    dd::Tracer tracer{*valid_config};
    // Note: This assumes that the benchmark is run from the repository root.
    sha256_traced("benchmark/tinycc", tracer);
  }
}
BENCHMARK(BM_TraceTinyCCSource);

} // namespace

BENCHMARK_MAIN();
