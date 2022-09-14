// These are tests for `Tracer`.  `Tracer` is responsible for creating root
// spans and for extracting spans from propagated trace context.

#include <datadog/error.h>
#include <datadog/net_util.h>
#include <datadog/null_collector.h>
#include <datadog/span.h>
#include <datadog/span_config.h>
#include <datadog/span_data.h>
#include <datadog/span_defaults.h>
#include <datadog/tag_propagation.h>
#include <datadog/trace_segment.h>
#include <datadog/tracer.h>
#include <datadog/tracer_config.h>

#include <iosfwd>
#include <optional>

#include "collectors.h"
#include "dict_readers.h"
#include "loggers.h"
#include "matchers.h"
#include "test.h"

namespace datadog {
namespace tracing {

std::ostream& operator<<(std::ostream& stream,
                         const std::optional<Error::Code>& code) {
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
  config.collector = std::make_shared<NullCollector>();
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
      std::string name;
      bool extract_datadog;
      bool extract_b3;
      std::unordered_map<std::string, std::string> headers;
      // Null means "don't expect an error."
      std::optional<Error::Code> expected_error;
    };

    auto test_case = GENERATE(values<TestCase>({
        {"no span", true, false, {}, Error::NO_SPAN_TO_EXTRACT},
        {"missing trace ID",
         true,
         false,
         {{"x-datadog-parent-id", "456"}},
         Error::MISSING_TRACE_ID},
        {"missing parent span ID",
         true,
         false,
         {{"x-datadog-trace-id", "123"}},
         Error::MISSING_PARENT_SPAN_ID},
        {"missing parent span ID, but it's ok because origin",
         true,
         false,
         {{"x-datadog-trace-id", "123"}, {"x-datadog-origin", "anything"}},
         std::nullopt},
        {"datadog and B3 agree",
         true,
         true,
         {{"x-datadog-trace-id", "15"},
          {"x-b3-traceid", "f"},
          {"x-datadog-parent-id", "14"},
          {"x-b3-spanid", "e"}},
         std::nullopt},
        {"datadog and B3 disagree on trace ID",
         true,
         true,
         {{"x-datadog-trace-id", "15"},
          {"x-b3-traceid", "f0"},
          {"x-datadog-parent-id", "14"},
          {"x-b3-spanid", "e"}},
         Error::INCONSISTENT_EXTRACTION_STYLES},
        {"datadog and B3 disagree on trace ID (2)",
         true,
         true,
         {{"x-datadog-trace-id", "15"},
          {"x-datadog-parent-id", "14"},
          {"x-b3-spanid", "e"}},
         Error::INCONSISTENT_EXTRACTION_STYLES},
        {"datadog and B3 disagree on parent ID",
         true,
         true,
         {{"x-datadog-trace-id", "15"},
          {"x-b3-traceid", "f"},
          {"x-datadog-parent-id", "13"},
          {"x-b3-spanid", "e"}},
         Error::INCONSISTENT_EXTRACTION_STYLES},
        {"datadog and B3 disagree on parent ID (2)",
         true,
         true,
         {{"x-datadog-trace-id", "15"},
          {"x-b3-traceid", "f"},
          {"x-datadog-parent-id", "13"}},
         Error::INCONSISTENT_EXTRACTION_STYLES},
        {"datadog and B3 disagree on sampling priority",
         true,
         true,
         {{"x-datadog-trace-id", "15"},
          {"x-b3-traceid", "f"},
          {"x-datadog-parent-id", "14"},
          {"x-b3-spanid", "e"},
          {"x-datadog-sampling-priority", "2"},
          {"x-b3-sampled", "1"}},
         Error::INCONSISTENT_EXTRACTION_STYLES},
        {"datadog and B3 disagree on sampling priority (2)",
         true,
         true,
         {{"x-datadog-trace-id", "15"},
          {"x-b3-traceid", "f"},
          {"x-datadog-parent-id", "14"},
          {"x-b3-spanid", "e"},
          {"x-datadog-sampling-priority", "2"}},
         Error::INCONSISTENT_EXTRACTION_STYLES},
        {"bad x-datadog-trace-id",
         true,
         false,
         {{"x-datadog-trace-id", "f"}, {"x-datadog-parent-id", "456"}},
         Error::INVALID_INTEGER},
        {"bad x-datadog-trace-id (2)",
         true,
         false,
         {{"x-datadog-trace-id", "99999999999999999999999999"},
          {"x-datadog-parent-id", "456"}},
         Error::OUT_OF_RANGE_INTEGER},
        {"bad x-datadog-parent-id",
         true,
         false,
         {{"x-datadog-parent-id", "f"}, {"x-datadog-trace-id", "456"}},
         Error::INVALID_INTEGER},
        {"bad x-datadog-parent-id (2)",
         true,
         false,
         {{"x-datadog-parent-id", "99999999999999999999999999"},
          {"x-datadog-trace-id", "456"}},
         Error::OUT_OF_RANGE_INTEGER},
        {"bad x-datadog-sampling-priority",
         true,
         false,
         {{"x-datadog-parent-id", "123"},
          {"x-datadog-trace-id", "456"},
          {"x-datadog-sampling-priority", "keep"}},
         Error::INVALID_INTEGER},
        {"bad x-datadog-sampling-priority (2)",
         true,
         false,
         {{"x-datadog-parent-id", "123"},
          {"x-datadog-trace-id", "456"},
          {"x-datadog-sampling-priority", "99999999999999999999999999"}},
         Error::OUT_OF_RANGE_INTEGER},
        {"bad x-b3-traceid",
         false,
         true,
         {{"x-b3-traceid", "0xdeadbeef"}, {"x-b3-spanid", "def"}},
         Error::INVALID_INTEGER},
        {"bad x-b3-traceid (2)",
         false,
         true,
         {{"x-b3-traceid", "ffffffffffffffffffffffffffffff"},
          {"x-b3-spanid", "def"}},
         Error::OUT_OF_RANGE_INTEGER},
        {"bad x-b3-spanid",
         false,
         true,
         {{"x-b3-spanid", "0xdeadbeef"}, {"x-b3-traceid", "def"}},
         Error::INVALID_INTEGER},
        {"bad x-b3-spanid (2)",
         false,
         true,
         {{"x-b3-spanid", "ffffffffffffffffffffffffffffff"},
          {"x-b3-traceid", "def"}},
         Error::OUT_OF_RANGE_INTEGER},
        {"bad x-b3-sampled",
         false,
         true,
         {{"x-b3-traceid", "abc"},
          {"x-b3-spanid", "def"},
          {"x-b3-sampled", "true"}},
         Error::INVALID_INTEGER},
        {"bad x-b3-sampled (2)",
         false,
         true,
         {{"x-b3-traceid", "abc"},
          {"x-b3-spanid", "def"},
          {"x-b3-sampled", "99999999999999999999999999"}},
         Error::OUT_OF_RANGE_INTEGER},
    }));

    CAPTURE(test_case.name);

    config.extraction_styles.datadog = test_case.extract_datadog;
    config.extraction_styles.b3 = test_case.extract_b3;
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
      std::string name;
      bool extract_datadog;
      bool extract_b3;
      std::unordered_map<std::string, std::string> headers;
      std::uint64_t expected_trace_id;
      std::optional<std::uint64_t> expected_parent_id;
      std::optional<int> expected_sampling_priority;
    };

    auto test_case = GENERATE(values<TestCase>({
        {"datadog style",
         true,
         false,
         {{"x-datadog-trace-id", "123"},
          {"x-datadog-parent-id", "456"},
          {"x-datadog-sampling-priority", "2"}},
         123,
         456,
         2},
        {"datadog style without sampling priority",
         true,
         false,
         {{"x-datadog-trace-id", "123"}, {"x-datadog-parent-id", "456"}},
         123,
         456,
         std::nullopt},
        {"datadog style without sampling priority and without parent ID",
         true,
         false,
         {{"x-datadog-trace-id", "123"}, {"x-datadog-origin", "whatever"}},
         123,
         std::nullopt,
         std::nullopt},
        {"B3 style",
         false,
         true,
         {{"x-b3-traceid", "abc"},
          {"x-b3-spanid", "def"},
          {"x-b3-sampled", "0"}},
         0xabc,
         0xdef,
         0},
        {"B3 style without sampling priority",
         false,
         true,
         {{"x-b3-traceid", "abc"}, {"x-b3-spanid", "def"}},
         0xabc,
         0xdef,
         std::nullopt},
        {"Datadog and B3 style together",
         true,
         true,
         {{"x-datadog-trace-id", "255"},
          {"x-datadog-parent-id", "14"},
          {"x-datadog-sampling-priority", "0"},
          {"x-b3-traceid", "ff"},
          {"x-b3-spanid", "e"},
          {"x-b3-sampled", "0"}},
         255,
         14,
         0},
        {"Datadog and B3 style together without sampling priority",
         true,
         true,
         {{"x-datadog-trace-id", "255"},
          {"x-datadog-parent-id", "14"},
          {"x-b3-traceid", "ff"},
          {"x-b3-spanid", "e"}},
         255,
         14,
         std::nullopt},
    }));

    CAPTURE(test_case.name);

    config.extraction_styles.datadog = test_case.extract_datadog;
    config.extraction_styles.b3 = test_case.extract_b3;
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
