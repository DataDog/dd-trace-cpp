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
// - `Collector::send` failure logs the error
// - finalization:
//   - root span:
//     - sampling priority
//     - "inject_max_size" propagation error if we tried to inject oversized
//     x-datadog-tags
//     - hostname if you got it
//     - anything in X-Datadog-Tags
//     - _dd.p.dm in particular (but only if sampling priority > 0)
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
