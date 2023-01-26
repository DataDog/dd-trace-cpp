// These are tests for `Tracer`.  `Tracer` is responsible for creating root
// spans and for extracting spans from propagated trace context.

#include <datadog/error.h>
#include <datadog/hex.h>
#include <datadog/null_collector.h>
#include <datadog/optional.h>
#include <datadog/parse_util.h>
#include <datadog/platform_util.h>
#include <datadog/span.h>
#include <datadog/span_config.h>
#include <datadog/span_data.h>
#include <datadog/span_defaults.h>
#include <datadog/tag_propagation.h>
#include <datadog/tags.h>
#include <datadog/trace_id.h>
#include <datadog/trace_segment.h>
#include <datadog/tracer.h>
#include <datadog/tracer_config.h>
#include <datadog/w3c_propagation.h>

#include <iosfwd>

#include "matchers.h"
#include "mocks/collectors.h"
#include "mocks/dict_readers.h"
#include "mocks/loggers.h"
#include "test.h"

namespace datadog {
namespace tracing {

std::ostream& operator<<(std::ostream& stream,
                         const Optional<Error::Code>& code) {
  if (code) {
    return stream << "Error::Code(" << int(*code) << ")";
  }
  return stream << "null";
}

}  // namespace tracing
}  // namespace datadog

using namespace datadog::tracing;

// Verify that the `.defaults.*` (`SpanDefaults`) properties of a tracer's
// configuration do determine the default properties of spans created by the
// tracer.
TEST_CASE("tracer span defaults") {
  TracerConfig config;
  config.defaults.service = "foosvc";
  config.defaults.service_type = "crawler";
  config.defaults.environment = "swamp";
  config.defaults.version = "first";
  config.defaults.name = "test.thing";
  config.defaults.tags = {{"some.thing", "thing value"},
                          {"another.thing", "another value"}};

  const auto collector = std::make_shared<MockCollector>();
  config.collector = collector;
  const auto logger = std::make_shared<MockLogger>();
  config.logger = logger;

  auto finalized_config = finalize_config(config);
  REQUIRE(finalized_config);

  Tracer tracer{*finalized_config};

  // Some of the sections below will override the defaults using `overrides`.
  // Make sure that the overridden values are different from the defaults,
  // so that we can distinguish between them.
  SpanConfig overrides;
  overrides.service = "barsvc";
  overrides.service_type = "wiggler";
  overrides.environment = "desert";
  overrides.version = "second";
  overrides.name = "test.another.thing";
  overrides.tags = {{"different.thing", "different"},
                    {"another.thing", "different value"}};

  REQUIRE(overrides.service != config.defaults.service);
  REQUIRE(overrides.service_type != config.defaults.service_type);
  REQUIRE(overrides.environment != config.defaults.environment);
  REQUIRE(overrides.version != config.defaults.version);
  REQUIRE(overrides.name != config.defaults.name);
  REQUIRE(overrides.tags != config.defaults.tags);

  // Some of the sections below create a span from extracted trace context.
  const std::unordered_map<std::string, std::string> headers{
      {"x-datadog-trace-id", "123"}, {"x-datadog-parent-id", "456"}};
  const MockDictReader reader{headers};

  SECTION("are honored in a root span") {
    {
      auto root = tracer.create_span();
      (void)root;
    }
    REQUIRE(logger->error_count() == 0);

    // Get the finished span from the collector and verify that its
    // properties have the configured default values.
    REQUIRE(collector->chunks.size() == 1);
    const auto& chunk = collector->chunks.front();
    REQUIRE(chunk.size() == 1);
    auto& root_ptr = chunk.front();
    REQUIRE(root_ptr);
    const auto& root = *root_ptr;

    REQUIRE(root.service == config.defaults.service);
    REQUIRE(root.service_type == config.defaults.service_type);
    REQUIRE(root.environment() == config.defaults.environment);
    REQUIRE(root.version() == config.defaults.version);
    REQUIRE(root.name == config.defaults.name);
    REQUIRE_THAT(root.tags, ContainsSubset(config.defaults.tags));
  }

  SECTION("can be overridden in a root span") {
    {
      auto root = tracer.create_span(overrides);
      (void)root;
    }
    REQUIRE(logger->error_count() == 0);

    // Get the finished span from the collector and verify that its
    // properties have the overridden values.
    REQUIRE(collector->chunks.size() == 1);
    const auto& chunk = collector->chunks.front();
    REQUIRE(chunk.size() == 1);
    auto& root_ptr = chunk.front();
    REQUIRE(root_ptr);
    const auto& root = *root_ptr;

    REQUIRE(root.service == overrides.service);
    REQUIRE(root.service_type == overrides.service_type);
    REQUIRE(root.environment() == overrides.environment);
    REQUIRE(root.version() == overrides.version);
    REQUIRE(root.name == overrides.name);
    REQUIRE_THAT(root.tags, ContainsSubset(overrides.tags));
  }

  SECTION("are honored in an extracted span") {
    {
      auto span = tracer.extract_span(reader);
      REQUIRE(span);
    }
    REQUIRE(logger->error_count() == 0);

    // Get the finished span from the collector and verify that its
    // properties have the configured default values.
    REQUIRE(collector->chunks.size() == 1);
    const auto& chunk = collector->chunks.front();
    REQUIRE(chunk.size() == 1);
    auto& span_ptr = chunk.front();
    REQUIRE(span_ptr);
    const auto& span = *span_ptr;

    REQUIRE(span.service == config.defaults.service);
    REQUIRE(span.service_type == config.defaults.service_type);
    REQUIRE(span.environment() == config.defaults.environment);
    REQUIRE(span.version() == config.defaults.version);
    REQUIRE(span.name == config.defaults.name);
    REQUIRE_THAT(span.tags, ContainsSubset(config.defaults.tags));
  }

  SECTION("can be overridden in an extracted span") {
    {
      auto span = tracer.extract_span(reader, overrides);
      REQUIRE(span);
    }
    REQUIRE(logger->error_count() == 0);

    // Get the finished span from the collector and verify that its
    // properties have the configured default values.
    REQUIRE(collector->chunks.size() == 1);
    const auto& chunk = collector->chunks.front();
    REQUIRE(chunk.size() == 1);
    auto& span_ptr = chunk.front();
    REQUIRE(span_ptr);
    const auto& span = *span_ptr;

    REQUIRE(span.service == overrides.service);
    REQUIRE(span.service_type == overrides.service_type);
    REQUIRE(span.environment() == overrides.environment);
    REQUIRE(span.version() == overrides.version);
    REQUIRE(span.name == overrides.name);
    REQUIRE_THAT(span.tags, ContainsSubset(overrides.tags));
  }

  SECTION("are honored in a child span") {
    {
      auto parent = tracer.create_span();
      auto child = parent.create_child();
      (void)child;
    }
    REQUIRE(logger->error_count() == 0);

    // Get the finished span from the collector and verify that its
    // properties have the configured default values.
    REQUIRE(collector->chunks.size() == 1);
    const auto& chunk = collector->chunks.front();
    // One span for the parent, and another for the child.
    REQUIRE(chunk.size() == 2);
    // The parent will be first, so the child is last.
    auto& child_ptr = chunk.back();
    REQUIRE(child_ptr);
    const auto& child = *child_ptr;

    REQUIRE(child.service == config.defaults.service);
    REQUIRE(child.service_type == config.defaults.service_type);
    REQUIRE(child.environment() == config.defaults.environment);
    REQUIRE(child.version() == config.defaults.version);
    REQUIRE(child.name == config.defaults.name);
    REQUIRE_THAT(child.tags, ContainsSubset(config.defaults.tags));
  }

  SECTION("can be overridden in a child span") {
    {
      auto parent = tracer.create_span();
      auto child = parent.create_child(overrides);
      (void)child;
    }
    REQUIRE(logger->error_count() == 0);

    // Get the finished span from the collector and verify that its
    // properties have the configured default values.
    REQUIRE(collector->chunks.size() == 1);
    const auto& chunk = collector->chunks.front();
    // One span for the parent, and another for the child.
    REQUIRE(chunk.size() == 2);
    // The parent will be first, so the child is last.
    auto& child_ptr = chunk.back();
    REQUIRE(child_ptr);
    const auto& child = *child_ptr;

    REQUIRE(child.service == overrides.service);
    REQUIRE(child.service_type == overrides.service_type);
    REQUIRE(child.environment() == overrides.environment);
    REQUIRE(child.version() == overrides.version);
    REQUIRE(child.name == overrides.name);
    REQUIRE_THAT(child.tags, ContainsSubset(overrides.tags));
  }
}

TEST_CASE("span extraction") {
  TracerConfig config;
  config.defaults.service = "testsvc";
  const auto collector = std::make_shared<MockCollector>();
  config.collector = collector;
  config.logger = std::make_shared<NullLogger>();

  SECTION(
      "extract_or_create yields a root span when there's no context to "
      "extract") {
    auto finalized_config = finalize_config(config);
    REQUIRE(finalized_config);
    Tracer tracer{*finalized_config};

    const std::unordered_map<std::string, std::string> no_headers;
    MockDictReader reader{no_headers};
    auto span = tracer.extract_or_create_span(reader);
    REQUIRE(span);
    REQUIRE(!span->parent_id());
  }

  SECTION("extraction failures") {
    struct TestCase {
      int line;
      std::string name;
      std::vector<PropagationStyle> extraction_styles;
      std::unordered_map<std::string, std::string> headers;
      // Null means "don't expect an error."
      Optional<Error::Code> expected_error;
    };

    auto test_case = GENERATE(values<TestCase>({
        {__LINE__,
         "no span",
         {PropagationStyle::DATADOG},
         {},
         Error::NO_SPAN_TO_EXTRACT},
        {__LINE__,
         "missing trace ID",
         {PropagationStyle::DATADOG},
         {{"x-datadog-parent-id", "456"}},
         Error::MISSING_TRACE_ID},
        {__LINE__,
         "missing parent span ID",
         {PropagationStyle::DATADOG},
         {{"x-datadog-trace-id", "123"}},
         Error::MISSING_PARENT_SPAN_ID},
        {__LINE__,
         "missing parent span ID, but it's ok because origin",
         {PropagationStyle::DATADOG},
         {{"x-datadog-trace-id", "123"}, {"x-datadog-origin", "anything"}},
         nullopt},
        {__LINE__,
         "bad x-datadog-trace-id",
         {PropagationStyle::DATADOG},
         {{"x-datadog-trace-id", "f"}, {"x-datadog-parent-id", "456"}},
         Error::INVALID_INTEGER},
        {__LINE__,
         "bad x-datadog-trace-id (2)",
         {PropagationStyle::DATADOG},
         {{"x-datadog-trace-id", "99999999999999999999999999"},
          {"x-datadog-parent-id", "456"}},
         Error::OUT_OF_RANGE_INTEGER},
        {__LINE__,
         "bad x-datadog-parent-id",
         {PropagationStyle::DATADOG},
         {{"x-datadog-parent-id", "f"}, {"x-datadog-trace-id", "456"}},
         Error::INVALID_INTEGER},
        {__LINE__,
         "bad x-datadog-parent-id (2)",
         {PropagationStyle::DATADOG},
         {{"x-datadog-parent-id", "99999999999999999999999999"},
          {"x-datadog-trace-id", "456"}},
         Error::OUT_OF_RANGE_INTEGER},
        {__LINE__,
         "bad x-datadog-sampling-priority",
         {PropagationStyle::DATADOG},
         {{"x-datadog-parent-id", "123"},
          {"x-datadog-trace-id", "456"},
          {"x-datadog-sampling-priority", "keep"}},
         Error::INVALID_INTEGER},
        {__LINE__,
         "bad x-datadog-sampling-priority (2)",
         {PropagationStyle::DATADOG},
         {{"x-datadog-parent-id", "123"},
          {"x-datadog-trace-id", "456"},
          {"x-datadog-sampling-priority", "99999999999999999999999999"}},
         Error::OUT_OF_RANGE_INTEGER},
        {__LINE__,
         "bad x-b3-traceid",
         {PropagationStyle::B3},
         {{"x-b3-traceid", "0xdeadbeef"}, {"x-b3-spanid", "def"}},
         Error::INVALID_INTEGER},
        {__LINE__,
         "bad x-b3-traceid (2)",
         {PropagationStyle::B3},
         {{"x-b3-traceid",
           "ffffffffffffffffffffffffffffffffffffffffffffffffffffffffff"},
          {"x-b3-spanid", "def"}},
         Error::OUT_OF_RANGE_INTEGER},
        {__LINE__,
         "bad x-b3-spanid",
         {PropagationStyle::B3},
         {{"x-b3-spanid", "0xdeadbeef"}, {"x-b3-traceid", "def"}},
         Error::INVALID_INTEGER},
        {__LINE__,
         "bad x-b3-spanid (2)",
         {PropagationStyle::B3},
         {{"x-b3-spanid", "ffffffffffffffffffffffffffffff"},
          {"x-b3-traceid", "def"}},
         Error::OUT_OF_RANGE_INTEGER},
        {__LINE__,
         "bad x-b3-sampled",
         {PropagationStyle::B3},
         {{"x-b3-traceid", "abc"},
          {"x-b3-spanid", "def"},
          {"x-b3-sampled", "true"}},
         Error::INVALID_INTEGER},
        {__LINE__,
         "bad x-b3-sampled (2)",
         {PropagationStyle::B3},
         {{"x-b3-traceid", "abc"},
          {"x-b3-spanid", "def"},
          {"x-b3-sampled", "99999999999999999999999999"}},
         Error::OUT_OF_RANGE_INTEGER},
    }));

    CAPTURE(test_case.line);
    CAPTURE(test_case.name);

    config.extraction_styles = test_case.extraction_styles;
    auto finalized_config = finalize_config(config);
    REQUIRE(finalized_config);
    Tracer tracer{*finalized_config};

    MockDictReader reader{test_case.headers};

    auto result = tracer.extract_span(reader);
    if (test_case.expected_error) {
      REQUIRE(!result);
      REQUIRE(result.error().code == test_case.expected_error);
    } else {
      REQUIRE(result);
    }

    // `extract_or_create_span` has similar behavior.
    if (test_case.expected_error != Error::NO_SPAN_TO_EXTRACT) {
      auto method = "extract_or_create_span";
      CAPTURE(method);
      auto result = tracer.extract_span(reader);
      if (test_case.expected_error) {
        REQUIRE(!result);
        REQUIRE(result.error().code == test_case.expected_error);
      } else {
        REQUIRE(result);
      }
    }
  }

  SECTION("extracted span has the expected properties") {
    struct TestCase {
      int line;
      std::string name;
      std::vector<PropagationStyle> extraction_styles;
      std::unordered_map<std::string, std::string> headers;
      TraceID expected_trace_id;
      Optional<std::uint64_t> expected_parent_id;
      Optional<int> expected_sampling_priority;
    };

    auto test_case = GENERATE(values<TestCase>({
        {__LINE__,
         "datadog style",
         {PropagationStyle::DATADOG},
         {{"x-datadog-trace-id", "123"},
          {"x-datadog-parent-id", "456"},
          {"x-datadog-sampling-priority", "2"}},
         TraceID(123),
         456,
         2},
        {__LINE__,
         "datadog style without sampling priority",
         {PropagationStyle::DATADOG},
         {{"x-datadog-trace-id", "123"}, {"x-datadog-parent-id", "456"}},
         TraceID(123),
         456,
         nullopt},
        {__LINE__,
         "datadog style without sampling priority and without parent ID",
         {PropagationStyle::DATADOG},
         {{"x-datadog-trace-id", "123"}, {"x-datadog-origin", "whatever"}},
         TraceID(123),
         nullopt,
         nullopt},
        {__LINE__,
         "B3 style",
         {PropagationStyle::B3},
         {{"x-b3-traceid", "abc"},
          {"x-b3-spanid", "def"},
          {"x-b3-sampled", "0"}},
         TraceID(0xabc),
         0xdef,
         0},
        {__LINE__,
         "B3 style without sampling priority",
         {PropagationStyle::B3},
         {{"x-b3-traceid", "abc"}, {"x-b3-spanid", "def"}},
         TraceID(0xabc),
         0xdef,
         nullopt},
        {__LINE__,
         "Datadog overriding B3",
         {PropagationStyle::DATADOG, PropagationStyle::B3},
         {{"x-datadog-trace-id", "255"},
          {"x-datadog-parent-id", "14"},
          {"x-datadog-sampling-priority", "0"},
          {"x-b3-traceid", "fff"},
          {"x-b3-spanid", "ef"},
          {"x-b3-sampled", "0"}},
         TraceID(255),
         14,
         0},
        {__LINE__,
         "Datadog overriding B3, without sampling priority",
         {PropagationStyle::DATADOG, PropagationStyle::B3},
         {{"x-datadog-trace-id", "255"},
          {"x-datadog-parent-id", "14"},
          {"x-b3-traceid", "fff"},
          {"x-b3-spanid", "ef"}},
         TraceID(255),
         14,
         nullopt},
        {__LINE__,
         "B3 after Datadog found no context",
         {PropagationStyle::DATADOG, PropagationStyle::B3},
         {{"x-b3-traceid", "ff"}, {"x-b3-spanid", "e"}},
         TraceID(0xff),
         0xe,
         nullopt},
        {__LINE__,
         "Datadog after B3 found no context",
         {PropagationStyle::B3, PropagationStyle::DATADOG},
         {{"x-b3-traceid", "fff"}, {"x-b3-spanid", "ef"}},
         TraceID(0xfff),
         0xef,
         nullopt},
    }));

    CAPTURE(test_case.line);
    CAPTURE(test_case.name);

    config.extraction_styles = test_case.extraction_styles;
    auto finalized_config = finalize_config(config);
    REQUIRE(finalized_config);
    Tracer tracer{*finalized_config};
    MockDictReader reader{test_case.headers};

    const auto checks = [](const TestCase& test_case, const Span& span) {
      REQUIRE(span.trace_id() == test_case.expected_trace_id);
      REQUIRE(span.parent_id() == test_case.expected_parent_id);
      if (test_case.expected_sampling_priority) {
        auto decision = span.trace_segment().sampling_decision();
        REQUIRE(decision);
        REQUIRE(decision->priority == test_case.expected_sampling_priority);
      } else {
        REQUIRE(!span.trace_segment().sampling_decision());
      }
    };

    auto span = tracer.extract_span(reader);
    REQUIRE(span);
    checks(test_case, *span);
    span = tracer.extract_or_create_span(reader);
    auto method = "extract_or_create_span";
    CAPTURE(method);
    REQUIRE(span);
    checks(test_case, *span);
  }

  SECTION("extraction can be disabled using the \"none\" style") {
    config.extraction_styles = {PropagationStyle::NONE};

    const auto finalized_config = finalize_config(config);
    REQUIRE(finalized_config);
    Tracer tracer{*finalized_config};
    const std::unordered_map<std::string, std::string> headers{
        // It doesn't matter which headers are present.
        // The "none" extraction style will not inspect them, and will return
        // the "no span to extract" error.
        {"X-Datadog-Trace-ID", "foo"},
        {"X-Datadog-Parent-ID", "bar"},
        {"X-Datadog-Sampling-Priority", "baz"},
        {"X-B3-TraceID", "foo"},
        {"X-B3-SpanID", "bar"},
        {"X-B3-Sampled", "baz"},
    };
    MockDictReader reader{headers};
    const auto result = tracer.extract_span(reader);
    REQUIRE(!result);
    REQUIRE(result.error().code == Error::NO_SPAN_TO_EXTRACT);
  }

  SECTION("W3C traceparent extraction") {
    const std::unordered_map<std::string, std::string> datadog_headers{
        {"x-datadog-trace-id", "18"},
        {"x-datadog-parent-id", "23"},
        {"x-datadog-sampling-priority", "-1"},
    };

    struct TestCase {
      int line;
      std::string name;
      Optional<std::string> traceparent;
      Optional<std::string> expected_error_tag_value = {};
      Optional<TraceID> expected_trace_id = {};
      Optional<std::uint64_t> expected_parent_id = {};
      Optional<int> expected_sampling_priority = {};
    };

    // clang-format off
    auto test_case = GENERATE(values<TestCase>({
        // https://www.w3.org/TR/trace-context/#examples-of-http-traceparent-headers
        {__LINE__, "valid: w3.org example 1",
         "00-4bf92f3577b34da6a3ce929d0e0e4736-00f067aa0ba902b7-01", // traceparent
         nullopt,
         *TraceID::parse_hex("4bf92f3577b34da6a3ce929d0e0e4736"), // expected_trace_id
         67667974448284343ULL, // expected_parent_id
         1}, // expected_sampling_priority

        {__LINE__, "valid: w3.org example 2",
         "00-4bf92f3577b34da6a3ce929d0e0e4736-00f067aa0ba902b7-00", // traceparent
         nullopt,
         *TraceID::parse_hex("4bf92f3577b34da6a3ce929d0e0e4736"), // expected_trace_id
         67667974448284343ULL, // expected_parent_id
         0}, // expected_sampling_priority

        {__LINE__, "valid: future version",
         "06-4bf92f3577b34da6a3ce929d0e0e4736-00f067aa0ba902b7-00", // traceparent
         nullopt,
         *TraceID::parse_hex("4bf92f3577b34da6a3ce929d0e0e4736"), // expected_trace_id
         67667974448284343ULL, // expected_parent_id
         0}, // expected_sampling_priority

        {__LINE__, "valid: future version with extra fields",
         "06-4bf92f3577b34da6a3ce929d0e0e4736-00f067aa0ba902b7-00-af-delta", // traceparent
         nullopt,
         *TraceID::parse_hex("4bf92f3577b34da6a3ce929d0e0e4736"), // expected_trace_id
         67667974448284343ULL, // expected_parent_id
         0}, // expected_sampling_priority

        {__LINE__, "no traceparent",
         nullopt}, // traceparent

        {__LINE__, "invalid: not enough fields",
         "06-4bf92f3577b34da6a3ce929d0e0e4736", // traceparent
         "malformed_traceparent"}, // expected_error_tag_value

        {__LINE__, "invalid: missing hyphen",
         "064bf92f3577b34da6a3ce929d0e0e4736-00f067aa0ba902b7-00", // traceparent
         "malformed_traceparent"}, // expected_error_tag_value

        {__LINE__, "invalid: extra data not preceded by hyphen",
         "06-4bf92f3577b34da6a3ce929d0e0e4736-00f067aa0ba902b7-00af-delta", // traceparent
         "malformed_traceparent"}, // expected_error_tag_value

        {__LINE__, "invalid: version",
         "ff-4bf92f3577b34da6a3ce929d0e0e4736-00f067aa0ba902b7-00", // traceparent
         "invalid_version"}, // expected_error_tag_value

        {__LINE__, "invalid: trace ID zero",
         "00-00000000000000000000000000000000-00f067aa0ba902b7-00", // traceparent
         "trace_id_zero"}, // expected_error_tag_value

        {__LINE__, "invalid: parent ID zero",
         "00-4bf92f3577b34da6a3ce929d0e0e4736-0000000000000000-00", // traceparent
         "parent_id_zero"}, // expected_error_tag_value
    }));
    // clang-format on

    CAPTURE(test_case.name);
    CAPTURE(test_case.line);

    config.extraction_styles = {PropagationStyle::W3C,
                                PropagationStyle::DATADOG};
    auto finalized_config = finalize_config(config);
    REQUIRE(finalized_config);
    Tracer tracer{*finalized_config};

    auto headers = datadog_headers;
    if (test_case.traceparent) {
      headers["traceparent"] = *test_case.traceparent;
    }
    MockDictReader reader{headers};

    // We can't `span->lookup(tags::internal::w3c_extraction_error)`, because
    // that tag is internal and will not be returned by `lookup`.  Instead, we
    // finish (destroy) the span to send it to a collector, and then inspect the
    // `SpanData` at the collector.
    Optional<SamplingDecision> decision;
    {
      auto span = tracer.extract_span(reader);
      REQUIRE(span);
      decision = span->trace_segment().sampling_decision();
    }

    REQUIRE(collector->span_count() == 1);
    const auto& span_data = collector->first_span();

    if (test_case.expected_error_tag_value) {
      const auto error_found =
          span_data.tags.find(tags::internal::w3c_extraction_error);
      REQUIRE(error_found != span_data.tags.end());
      REQUIRE(error_found->second == *test_case.expected_error_tag_value);
      // Extraction would have fallen back to the next configured style (Datadog
      // -- see `config.extraction_styles`, above), and so the span's properties
      // should match `datadog_headers`, above.
      REQUIRE(span_data.trace_id == 18);
      REQUIRE(span_data.parent_id == 23);
      REQUIRE(decision);
      REQUIRE(decision->origin == SamplingDecision::Origin::EXTRACTED);
      REQUIRE(decision->priority == -1);
    } else if (!test_case.traceparent) {
      // There was no error extracting W3C context, but there was none to
      // extract.
      // Extraction would have fallen back to the next configured style (Datadog
      // -- see `config.extraction_styles`, above), and so the span's properties
      // should match `datadog_headers`, above.
      REQUIRE(span_data.trace_id == 18);
      REQUIRE(span_data.parent_id == 23);
      REQUIRE(decision);
      REQUIRE(decision->origin == SamplingDecision::Origin::EXTRACTED);
      REQUIRE(decision->priority == -1);
    } else {
      // W3C context was successfully extracted from traceparent header.
      REQUIRE(span_data.trace_id == *test_case.expected_trace_id);
      REQUIRE(span_data.parent_id == *test_case.expected_parent_id);
      REQUIRE(decision);
      REQUIRE(decision->origin == SamplingDecision::Origin::EXTRACTED);
      REQUIRE(decision->priority == *test_case.expected_sampling_priority);
    }
  }

  SECTION("W3C tracestate extraction") {
    // Ideally this would test the _behavior_ of W3C tracestate extraction,
    // rather than its implementation.
    // However, some of the effects of W3C tracestate extraction cannot be
    // observed except by injecting trace context, and there's a separate test
    // for W3C tracestate injection (in `test_span.cpp`).
    // Here we test the tracestate portion of the `extract_w3c` function,
    // declared in `w3c_propagation.h`.
    struct TestCase {
      int line;
      std::string name;
      std::string traceparent;
      Optional<std::string> tracestate;
      Optional<int> expected_sampling_priority = {};
      Optional<std::string> expected_origin = {};
      std::vector<std::pair<std::string, std::string>> expected_trace_tags = {};
      Optional<std::string> expected_additional_w3c_tracestate = {};
      Optional<std::string> expected_additional_datadog_w3c_tracestate = {};
    };

    static const std::string traceparent_prefix =
        "00-00000000000000000000000000000001-0000000000000001-0";
    static const std::string traceparent_drop = traceparent_prefix + "0";
    static const std::string traceparent_keep = traceparent_prefix + "1";
    // clang-format off
    auto test_case = GENERATE(values<TestCase>({
        {__LINE__, "no tracestate",
         traceparent_drop, // traceparent
         nullopt, // tracestate
         0}, // expected_sampling_priority

        {__LINE__, "empty tracestate",
         traceparent_drop, // traceparent
         "", // tracestate
         0}, // expected_sampling_priority

        {__LINE__, "no dd entry",
         traceparent_drop, // traceparent
         "foo=hello,@thingy/thing=wah;wah;wah", // tracestate
         0, // expected_sampling_priority
         nullopt, // expected_origin
         {}, // expected_trace_tags
         "foo=hello,@thingy/thing=wah;wah;wah", // expected_additional_w3c_tracestate
         nullopt}, // expected_additional_datadog_w3c_tracestate

        {__LINE__, "empty entry",
         traceparent_drop, // traceparent
         "foo=hello,,bar=thing", // tracestate
         0, // expected_sampling_priority
         nullopt, // expected_origin
         {}, // expected_trace_tags
         "foo=hello,,bar=thing", // expected_additional_w3c_tracestate
         nullopt}, // expected_additional_datadog_w3c_tracestate

        {__LINE__, "malformed entry",
         traceparent_drop, // traceparent
         "foo=hello,chicken,bar=thing", // tracestate
         0, // expected_sampling_priority
         nullopt, // expected_origin
         {}, // expected_trace_tags
         "foo=hello,chicken,bar=thing", // expected_additional_w3c_tracestate
         nullopt}, // expected_additional_datadog_w3c_tracestate

        {__LINE__, "stuff before dd entry",
         traceparent_drop, // traceparent
         "foo=hello,bar=baz,dd=", // tracestate
         0, // expected_sampling_priority
         nullopt, // expected_origin
         {}, // expected_trace_tags
         "foo=hello,bar=baz", // expected_additional_w3c_tracestate
         nullopt}, // expected_additional_datadog_w3c_tracestate

        {__LINE__, "stuff after dd entry",
         traceparent_drop, // traceparent
         "dd=,foo=hello,bar=baz", // tracestate
         0, // expected_sampling_priority
         nullopt, // expected_origin
         {}, // expected_trace_tags
         "foo=hello,bar=baz", // expected_additional_w3c_tracestate
         nullopt}, // expected_additional_datadog_w3c_tracestate

        {__LINE__, "stuff before and after dd entry",
         traceparent_drop, // traceparent
         "chicken=yes,nuggets=yes,dd=,foo=hello,bar=baz", // tracestate
         0, // expected_sampling_priority
         nullopt, // expected_origin
         {}, // expected_trace_tags
         "chicken=yes,nuggets=yes,foo=hello,bar=baz", // expected_additional_w3c_tracestate
         nullopt}, // expected_additional_datadog_w3c_tracestate

        {__LINE__, "dd entry with empty subentries",
         traceparent_drop, // traceparent
         "dd=foo:bar;;;;;baz:bam;;;", // tracestate
         0, // expected_sampling_priority
         nullopt, // expected_origin
         {}, // expected_trace_tags
         nullopt, // expected_additional_w3c_tracestate
         "foo:bar;baz:bam"}, // expected_additional_datadog_w3c_tracestate

        {__LINE__, "dd entry with malformed subentries",
         traceparent_drop, // traceparent
         "dd=foo:bar;chicken;chicken;baz:bam;chicken", // tracestate
         0, // expected_sampling_priority
         nullopt, // expected_origin
         {}, // expected_trace_tags
         nullopt, // expected_additional_w3c_tracestate
         "foo:bar;baz:bam"}, // expected_additional_datadog_w3c_tracestate

         {__LINE__, "origin, trace tags, and extra fields",
          traceparent_drop, // traceparent
          "dd=o:France;t.foo:thing1;t.bar:thing2;x:wow;y:wow", // tracestate
          0, // expected_sampling_priority
          "France", // expected_origin
          {{"_dd.p.foo", "thing1"}, {"_dd.p.bar", "thing2"}}, // expected_trace_tags
          nullopt, // expected_additional_w3c_tracestate
          "x:wow;y:wow"}, // expected_additional_datadog_w3c_tracestate

        {__LINE__, "_dd.p.tid trace tag is ignored",
         traceparent_drop, // traceparent
         "dd=t.tid:deadbeef;t.foo:bar", // tracestate
         0, // expected_sampling_priority
         nullopt, // expected_origin
         {{"_dd.p.foo", "bar"}}, // expected_trace_tags
         nullopt, // expected_additional_w3c_tracestate
         nullopt}, // expected_additional_datadog_w3c_tracestate

         {__LINE__, "traceparent and tracestate sampling agree (1/4)",
          traceparent_drop, // traceparent
          "dd=s:0", // tracestate
          0}, // expected_sampling_priority

         {__LINE__, "traceparent and tracestate sampling agree (2/4)",
          traceparent_drop, // traceparent
          "dd=s:-1", // tracestate
          -1}, // expected_sampling_priority

         {__LINE__, "traceparent and tracestate sampling agree (3/4)",
          traceparent_keep, // traceparent
          "dd=s:1", // tracestate
          1}, // expected_sampling_priority

         {__LINE__, "traceparent and tracestate sampling agree (4/4)",
          traceparent_keep, // traceparent
          "dd=s:2", // tracestate
          2}, // expected_sampling_priority

         {__LINE__, "traceparent and tracestate sampling disagree (1/4)",
          traceparent_drop, // traceparent
          "dd=s:1", // tracestate
          0}, // expected_sampling_priority

         {__LINE__, "traceparent and tracestate sampling disagree (2/4)",
          traceparent_drop, // traceparent
          "dd=s:2", // tracestate
          0}, // expected_sampling_priority

         {__LINE__, "traceparent and tracestate sampling disagree (3/4)",
          traceparent_keep, // traceparent
          "dd=s:0", // tracestate
          1}, // expected_sampling_priority

         {__LINE__, "traceparent and tracestate sampling disagree (4/4)",
          traceparent_keep, // traceparent
          "dd=s:-1", // tracestate
          1}, // expected_sampling_priority

         {__LINE__, "invalid sampling priority (1/2)",
          traceparent_drop, // traceparent
          "dd=s:oops", // tracestate
          0}, // expected_sampling_priority

         {__LINE__, "invalid sampling priority (2/2)",
          traceparent_keep, // traceparent
          "dd=s:oops", // tracestate
          1}, // expected_sampling_priority
    }));
    // clang-format on

    CAPTURE(test_case.name);
    CAPTURE(test_case.line);
    CAPTURE(test_case.traceparent);
    CAPTURE(test_case.tracestate);

    std::unordered_map<std::string, std::string> span_tags;
    MockLogger logger;
    CAPTURE(logger.entries);
    CAPTURE(span_tags);

    std::unordered_map<std::string, std::string> headers;
    headers["traceparent"] = test_case.traceparent;
    if (test_case.tracestate) {
      headers["tracestate"] = *test_case.tracestate;
    }
    MockDictReader reader{headers};

    const auto extracted = extract_w3c(reader, span_tags, logger);
    REQUIRE(extracted);

    REQUIRE(extracted->origin == test_case.expected_origin);
    REQUIRE(extracted->trace_tags == test_case.expected_trace_tags);
    REQUIRE(extracted->sampling_priority ==
            test_case.expected_sampling_priority);
    REQUIRE(extracted->additional_w3c_tracestate ==
            test_case.expected_additional_w3c_tracestate);
    REQUIRE(extracted->additional_datadog_w3c_tracestate ==
            test_case.expected_additional_datadog_w3c_tracestate);

    REQUIRE(logger.entries.empty());
    REQUIRE(span_tags.empty());
  }

  SECTION("x-datadog-tags") {
    auto finalized_config = finalize_config(config);
    REQUIRE(finalized_config);
    Tracer tracer{*finalized_config};

    std::unordered_map<std::string, std::string> headers{
        {"x-datadog-trace-id", "123"}, {"x-datadog-parent-id", "456"}};
    MockDictReader reader{headers};

    SECTION("extraction succeeds when x-datadog-tags is valid") {
      const std::string header_value = "foo=bar,_dd.something=yep-yep";
      REQUIRE(decode_tags(header_value));
      headers["x-datadog-tags"] = header_value;
      REQUIRE(tracer.extract_span(reader));
    }

    SECTION("extraction succeeds when x-datadog-tags is empty") {
      const std::string header_value = "";
      REQUIRE(decode_tags(header_value));
      headers["x-datadog-tags"] = header_value;
      REQUIRE(tracer.extract_span(reader));
    }

    SECTION("extraction succeeds when x-datadog-tags is invalid") {
      const std::string header_value = "this is missing an equal sign";
      REQUIRE(!decode_tags(header_value));
      headers["x-datadog-tags"] = header_value;
      REQUIRE(tracer.extract_span(reader));
    }
  }
}

TEST_CASE("report hostname") {
  TracerConfig config;
  config.defaults.service = "testsvc";
  config.collector = std::make_shared<NullCollector>();
  config.logger = std::make_shared<NullLogger>();

  SECTION("is off by default") {
    auto finalized_config = finalize_config(config);
    REQUIRE(finalized_config);
    Tracer tracer{*finalized_config};
    REQUIRE(!tracer.create_span().trace_segment().hostname());
  }

  SECTION("is available when enabled") {
    config.report_hostname = true;
    auto finalized_config = finalize_config(config);
    REQUIRE(finalized_config);
    Tracer tracer{*finalized_config};
    REQUIRE(tracer.create_span().trace_segment().hostname() == get_hostname());
  }
}

TEST_CASE("create 128-bit trace IDs") {
  TracerConfig config;
  config.defaults.service = "testsvc";
  config.trace_id_128_bit = true;
  const auto collector = std::make_shared<MockCollector>();
  config.collector = collector;
  const auto logger = std::make_shared<MockLogger>();
  config.logger = logger;
  config.extraction_styles.clear();
  config.extraction_styles.push_back(PropagationStyle::W3C);
  config.extraction_styles.push_back(PropagationStyle::DATADOG);
  config.extraction_styles.push_back(PropagationStyle::B3);
  const auto finalized = finalize_config(config);
  REQUIRE(finalized);
  Tracer tracer{*finalized};

  SECTION("are generated") {
    const auto span = tracer.create_span();
    // The chance that it's zero is ~2**(-64), which I'm willing to neglect.
    REQUIRE(span.trace_id().high != 0);
  }

  SECTION("result in _dd.p.tid trace tag being sent to collector") {
    TraceID generated_id;
    {
      const auto span = tracer.create_span();
      generated_id = span.trace_id();
    }
    CAPTURE(logger->entries);
    REQUIRE(logger->error_count() == 0);
    REQUIRE(collector->span_count() == 1);
    const auto& span = collector->first_span();
    const auto found = span.tags.find(tags::internal::trace_id_high);
    REQUIRE(found != span.tags.end());
    const auto high = parse_uint64(found->second, 16);
    REQUIRE(high);
    REQUIRE(*high == generated_id.high);
  }

  SECTION("extracted from W3C") {
    std::unordered_map<std::string, std::string> headers;
    headers["traceparent"] =
        "00-deadbeefdeadbeefcafebabecafebabe-0000000000000001-01";
    MockDictReader reader{headers};
    const auto span = tracer.extract_span(reader);
    CAPTURE(logger->entries);
    REQUIRE(logger->error_count() == 0);
    REQUIRE(span);
    REQUIRE(hex(span->trace_id().high) == "deadbeefdeadbeef");
  }

  SECTION("extracted from Datadog (_dd.p.tid)") {
    std::unordered_map<std::string, std::string> headers;
    headers["x-datadog-trace-id"] = "4";
    headers["x-datadog-parent-id"] = "42";
    headers["x-datadog-tags"] = "_dd.p.tid=beef";
    MockDictReader reader{headers};
    const auto span = tracer.extract_span(reader);
    CAPTURE(logger->entries);
    REQUIRE(logger->error_count() == 0);
    REQUIRE(span);
    REQUIRE(span->trace_id().hex_padded() ==
            "000000000000beef0000000000000004");
  }

  SECTION("extracted from B3") {
    std::unordered_map<std::string, std::string> headers;
    headers["x-b3-traceid"] = "deadbeefdeadbeefcafebabecafebabe";
    headers["x-b3-spanid"] = "42";
    MockDictReader reader{headers};
    const auto span = tracer.extract_span(reader);
    CAPTURE(logger->entries);
    REQUIRE(logger->error_count() == 0);
    REQUIRE(span);
    REQUIRE(hex(span->trace_id().high) == "deadbeefdeadbeef");
  }
}
