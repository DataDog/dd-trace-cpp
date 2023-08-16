#include <benchmark/benchmark.h>

#include <datadog/logger.h>
#include <datadog/null_collector.h>
#include <datadog/span_config.h>
#include <datadog/tracer.h>
#include <datadog/tracer_config.h>

namespace {

namespace dd = datadog::tracing;

// `NullLogger` doesn't log. It avoids `log_startup` spam in the benchmark.
struct NullLogger : public dd::Logger {
  void log_error(const LogFunc&) override {}
  void log_startup(const LogFunc&) override {}
  void log_error(const dd::Error&) override {}
  void log_error(dd::StringView) override {}
};

// TODO: document
void withOrWithoutGlobbing(benchmark::State& state, const std::vector<dd::TraceSamplerConfig::Rule>& rules) {
  dd::TracerConfig config;
  config.defaults.service = "benchmark";
  config.logger = std::make_shared<NullLogger>();
  config.collector = std::make_shared<dd::NullCollector>();
  config.trace_sampler.rules = rules;
  const auto valid_config = dd::finalize_config(config);
  dd::Tracer tracer{*valid_config};

  for (auto _ : state) {
    dd::SpanConfig config;
    config.name = "aaaaaaaaaaaaaaaa";
    (void)tracer.create_span(config);
  }
}

// TODO: document
void BM_WithGlobbing(benchmark::State& state) {
  std::vector<dd::TraceSamplerConfig::Rule> rules;
  dd::TraceSamplerConfig::Rule rule;
  rule.name = "a*a*a";
  rules.push_back(rule);

  withOrWithoutGlobbing(state, rules);
}
BENCHMARK(BM_WithGlobbing);

// TODO: document
void BM_WithoutGlobbing(benchmark::State& state) {
  const std::vector<dd::TraceSamplerConfig::Rule> no_rules{};
  withOrWithoutGlobbing(state, no_rules);
}
BENCHMARK(BM_WithoutGlobbing);

} // namespace

