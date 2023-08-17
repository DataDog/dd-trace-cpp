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
void spanRuleWithOrWithoutGlobbing(
    benchmark::State& state,
    const std::vector<dd::SpanSamplerConfig::Rule>& rules, int span_count) {
  dd::TracerConfig config;
  config.defaults.service = "benchmark";
  config.logger = std::make_shared<NullLogger>();
  config.collector = std::make_shared<dd::NullCollector>();
  config.trace_sampler.sample_rate =
      0;  // drop all traces, so that we use span sampling
  config.span_sampler.rules = rules;
  const auto valid_config = dd::finalize_config(config);
  dd::Tracer tracer{*valid_config};

  std::vector<dd::Span> spans;
  spans.reserve(span_count);

  for (auto _ : state) {
    dd::SpanConfig config;
    config.name = "aaaaaaaaaaaaaaaa";
    spans.push_back(tracer.create_span(config));
    for (int i = 0; i < span_count; ++i) {
      spans.push_back(spans.back().create_child(config));
    }
    spans.clear();
  }
}

// TODO: document
void traceRuleWithOrWithoutGlobbing(
    benchmark::State& state,
    const std::vector<dd::TraceSamplerConfig::Rule>& rules) {
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
void BM_TraceRuleWithGlobbing(benchmark::State& state) {
  std::vector<dd::TraceSamplerConfig::Rule> rules;
  dd::TraceSamplerConfig::Rule rule;
  rule.name = "a*a*a";
  rules.push_back(rule);

  traceRuleWithOrWithoutGlobbing(state, rules);
}
BENCHMARK(BM_TraceRuleWithGlobbing);

// TODO: document
void BM_TraceRuleWithoutGlobbing(benchmark::State& state) {
  const std::vector<dd::TraceSamplerConfig::Rule> no_rules{};
  traceRuleWithOrWithoutGlobbing(state, no_rules);
}
BENCHMARK(BM_TraceRuleWithoutGlobbing);

// TODO: document
void BM_SpanRuleWithGlobbing1000SpansTricky(benchmark::State& state) {
  std::vector<dd::SpanSamplerConfig::Rule> rules;
  dd::SpanSamplerConfig::Rule rule;
  rule.name = "a*a*a";
  rules.push_back(rule);

  spanRuleWithOrWithoutGlobbing(state, rules, 1000);
}
BENCHMARK(BM_SpanRuleWithGlobbing1000SpansTricky);

// TODO: document
void BM_SpanRuleWithoutGlobbing1000Spans(benchmark::State& state) {
  const std::vector<dd::SpanSamplerConfig::Rule> no_rules{};
  spanRuleWithOrWithoutGlobbing(state, no_rules, 1000);
}
BENCHMARK(BM_SpanRuleWithoutGlobbing1000Spans);

// TODO: document
void BM_SpanRuleWithGlobbing100SpansTricky(benchmark::State& state) {
  std::vector<dd::SpanSamplerConfig::Rule> rules;
  dd::SpanSamplerConfig::Rule rule;
  rule.name = "a*a*a";
  rules.push_back(rule);

  spanRuleWithOrWithoutGlobbing(state, rules, 100);
}
BENCHMARK(BM_SpanRuleWithGlobbing100SpansTricky);

// TODO: document
void BM_SpanRuleWithoutGlobbing100Spans(benchmark::State& state) {
  const std::vector<dd::SpanSamplerConfig::Rule> no_rules{};
  spanRuleWithOrWithoutGlobbing(state, no_rules, 100);
}
BENCHMARK(BM_SpanRuleWithoutGlobbing100Spans);

// TODO: document
void BM_SpanRuleWithGlobbing100SpansEasy(benchmark::State& state) {
  std::vector<dd::SpanSamplerConfig::Rule> rules;
  dd::SpanSamplerConfig::Rule rule;
  rule.name = "aaaaaaaaaaaaaaaa";
  rules.push_back(rule);

  spanRuleWithOrWithoutGlobbing(state, rules, 100);
}
BENCHMARK(BM_SpanRuleWithGlobbing100SpansEasy);

// TODO: document
void BM_10SpanRulesWithGlobbing100SpansEasy(benchmark::State& state) {
  std::vector<dd::SpanSamplerConfig::Rule> rules;
  dd::SpanSamplerConfig::Rule rule;
  for (int i = 0; i < 99; ++i) {
    rule.name = "aaaaaaaaaaaaaaax";
    rules.push_back(rule);
  }
  rule.name = "aaaaaaaaaaaaaaaa";
  rules.push_back(rule);

  spanRuleWithOrWithoutGlobbing(state, rules, 100);
}
BENCHMARK(BM_10SpanRulesWithGlobbing100SpansEasy);

// TODO: document
void BM_10TrivialSpanRulesWithGlobbing100SpansEasy(benchmark::State& state) {
  std::vector<dd::SpanSamplerConfig::Rule> rules;
  dd::SpanSamplerConfig::Rule rule;
  for (int i = 0; i < 100; ++i) {
    rule.name = "x";
    rule.service = "x";
    rule.resource = "x";
    rules.push_back(rule);
  }

  spanRuleWithOrWithoutGlobbing(state, rules, 100);
}
BENCHMARK(BM_10TrivialSpanRulesWithGlobbing100SpansEasy);

}  // namespace
