#include <datadog/tags.h>
#include <datadog/trace_segment.h>
#include <datadog/tracer.h>
#include <datadog/tracer_config.h>

#include "collectors.h"
#include "dict_readers.h"
#include "dict_writers.h"
#include "loggers.h"
#include "test.h"

using namespace datadog::tracing;

// TODO:
// - accessors
//    ✅ hostname
//    ✅ defaults
//    ✅ origin
//    ✅ sampling_decision
//    ✅ logger
// ✅ `Collector::send` failure logs the error
// - finalization:
//   - root span:
//     - sampling priority
//     ✅ "inject_max_size" propagation error if we tried to inject oversized
//       x-datadog-tags
//     - hostname if you got it
//     - anything in X-Datadog-Tags
//       - _dd.p.dm in particular (but only if sampling priority > 0)
//     - if agent made sampling decision, agent rate
//     - if rule/sample_Rate made sampling decision, rule rate
//     - if rule limiter was consulted in sampling decision, limiter effective
//     rate
// - all spans:
//     - origin if you got it

TEST_CASE("TraceSegment accessors") {
  TracerConfig config;
  config.defaults.service = "testsvc";
  const auto collector = std::make_shared<MockCollector>();
  config.collector = collector;
  config.logger = std::make_shared<MockLogger>();

  SECTION("hostname") {
    config.report_hostname = GENERATE(true, false);

    auto finalized = finalize_config(config);
    REQUIRE(finalized);
    Tracer tracer{*finalized};
    auto span = tracer.create_span();

    auto hostname = span.trace_segment().hostname();
    if (config.report_hostname) {
      REQUIRE(hostname);
    } else {
      REQUIRE(!hostname);
    }
  }

  SECTION("defaults") {
    config.defaults.name = "wobble";
    config.defaults.service_type = "fake";
    config.defaults.version = "v0";
    config.defaults.environment = "test";
    config.defaults.tags = {{"hello", "world"}, {"foo", "bar"}};

    auto finalized = finalize_config(config);
    REQUIRE(finalized);
    Tracer tracer{*finalized};
    auto span = tracer.create_span();

    REQUIRE(span.trace_segment().defaults() == config.defaults);
  }

  SECTION("origin") {
    auto finalized = finalize_config(config);
    REQUIRE(finalized);
    Tracer tracer{*finalized};

    const std::unordered_map<std::string, std::string> headers{
        {"x-datadog-trace-id", "123"},
        {"x-datadog-parent-id", "456"},
        {"x-datadog-origin", "Unalaska"}};
    MockDictReader reader{headers};
    auto span = tracer.extract_span(reader);
    REQUIRE(span);
    REQUIRE(span->trace_segment().origin() == "Unalaska");
  }

  SECTION("sampling_decision") {
    auto finalized = finalize_config(config);
    REQUIRE(finalized);
    Tracer tracer{*finalized};

    SECTION("default create_span  →  no decision") {
      auto span = tracer.create_span();
      auto decision = span.trace_segment().sampling_decision();
      REQUIRE(!decision);
    }

    SECTION("after injecting at least once  →  local decision") {
      auto span = tracer.create_span();
      MockDictWriter writer;
      span.inject(writer);
      auto decision = span.trace_segment().sampling_decision();
      REQUIRE(decision);
      REQUIRE(decision->origin == SamplingDecision::Origin::LOCAL);
    }

    SECTION("extracted priority  →  extracted decision") {
      const std::unordered_map<std::string, std::string> headers{
          {"x-datadog-trace-id", "123"},
          {"x-datadog-parent-id", "456"},
          {"x-datadog-sampling-priority", "7"}};  // 😯
      MockDictReader reader{headers};
      auto span = tracer.extract_span(reader);
      REQUIRE(span);
      auto decision = span->trace_segment().sampling_decision();
      REQUIRE(decision);
      REQUIRE(decision->origin == SamplingDecision::Origin::EXTRACTED);
    }

    SECTION("override on segment  →  local decision") {
      auto span = tracer.create_span();
      span.trace_segment().override_sampling_priority(-10);  // 😵
      auto decision = span.trace_segment().sampling_decision();
      REQUIRE(decision);
      REQUIRE(decision->origin == SamplingDecision::Origin::LOCAL);
    }
  }

  SECTION("logger") {
    auto finalized = finalize_config(config);
    REQUIRE(finalized);
    Tracer tracer{*finalized};
    auto span = tracer.create_span();
    REQUIRE(&span.trace_segment().logger() == config.logger.get());
  }
}

TEST_CASE("When Collector::send fails, TraceSegment logs the error.") {
  TracerConfig config;
  config.defaults.service = "testsvc";
  const auto collector = std::make_shared<FailureCollector>();
  config.collector = collector;
  const auto logger = std::make_shared<MockLogger>();
  config.logger = logger;

  auto finalized = finalize_config(config);
  REQUIRE(finalized);
  Tracer tracer{*finalized};
  {
    // The only span, created and then destroyed, so that the `TraceSegment`
    // will `.send` it to the `Collector`, which will fail.
    auto span = tracer.create_span();
    (void)span;
  }
  REQUIRE(logger->error_count() == 1);
  REQUIRE(logger->first_error().code == collector->failure.code);
}

TEST_CASE("TraceSegment finalization of spans") {
  TracerConfig config;
  config.defaults.service = "testsvc";
  const auto collector = std::make_shared<MockCollector>();
  config.collector = collector;
  config.logger = std::make_shared<MockLogger>();

  SECTION("root span") {
    SECTION(
        "'inject_max_size' propagation error if X-Datadog-Tags oversized on "
        "inject") {
      auto finalized = finalize_config(config);
      REQUIRE(finalized);
      Tracer tracer{*finalized};

      // Make a very large X-Datadog-Tags value.
      std::string trace_tags_value = "foo=bar";
      for (int i = 0; i < 10'000; ++i) {
        trace_tags_value += ',';
        trace_tags_value += std::to_string(i);
        trace_tags_value += '=';
        trace_tags_value += std::to_string(2 * i);
      }

      const std::unordered_map<std::string, std::string> headers{
          {"x-datadog-trace-id", "123"},
          {"x-datadog-parent-id", "456"},
          {"x-datadog-tags", trace_tags_value}};
      MockDictReader reader{headers};
      {
        auto span = tracer.extract_span(reader);
        REQUIRE(span);

        // Injecting the oversized X-Datadog-Tags will make `TraceSegment` note
        // an error, which it will later tag on the root span.
        MockDictWriter writer;
        span->inject(writer);
        REQUIRE(writer.items.count("x-datadog-tags") == 0);
      }

      REQUIRE(collector->first_span().tags.at(
                  tags::internal::propagation_error) == "inject_max_size");
    }

    // TODO
  }
}
