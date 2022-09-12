// These are tests for `Tracer`.  `Tracer` is responsible for creating root
// spans and for extracting spans from propagated trace context.

#include "test.h"

#include <datadog/span.h>
#include <datadog/span_config.h>
#include <datadog/span_data.h>
#include <datadog/span_defaults.h>
#include <datadog/tracer.h>
#include <datadog/tracer_config.h>

#include "collectors.h"
#include "dict_readers.h"
#include "loggers.h"
#include "matchers.h"

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
    config.defaults.tags = {{"some.thing", "thing value"}, {"another.thing", "another value"}};

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
    overrides.tags = {{"different.thing", "different"}, {"another.thing", "different value"}};

    REQUIRE(overrides.service != config.defaults.service);
    REQUIRE(overrides.service_type != config.defaults.service_type);
    REQUIRE(overrides.environment != config.defaults.environment);
    REQUIRE(overrides.version != config.defaults.version);
    REQUIRE(overrides.name != config.defaults.name);
    REQUIRE(overrides.tags != config.defaults.tags);
    
    // Some of the sections below create a span from extracted trace context.
    const std::unordered_map<std::string, std::string> headers{{"x-datadog-trace-id", "123"}, {"x-datadog-parent-id", "456"}};
    const MockDictReader reader{headers};

    SECTION("are honored in a root span") {
        {
            auto root = tracer.create_span();
            (void)root;
        }
        
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

// Further things to verify:
//
// - extract_or_create yields a root span when there's no context to extract
// - extract returns an error in the following situations:
//     - no parent ID and no trace ID (no context to extract)
//     - parent ID without trace ID
//     - trace ID without parent ID and without origin
//         - but _no error_ when trace ID without parent ID and with origin
//     - both Datadog and B3 extractions styles are set, but they disagree:
//         - difference trace ID
//         - different parent ID
//         - different sampling priority
// - extracted information is as expected when using Datadog style
// - extracted information is as expected when using B3 style
// - extracted information is as expected when using consistent Datadog and B3 style
//     - which info? trace ID, parent span ID, sampling priority
