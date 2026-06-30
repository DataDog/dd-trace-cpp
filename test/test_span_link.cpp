// Tests for `SpanLink` msgpack serialization. The on-the-wire field names and
// omission rules must match the other Datadog tracers (dd-trace-go, -py, -rs).

#include <datadog/span_link.h>

#include <cstdint>
#include <string>

#include <datadog/json.hpp>

#include "test.h"

using namespace datadog::tracing;

#define TEST_SPAN_LINK(x) TEST_CASE(x, "[span_link]")

namespace {
// Encode a single link and decode it back to JSON for inspection.
nlohmann::json encode_to_json(const SpanLink& link) {
  std::string buffer;
  const auto result = msgpack_encode(buffer, link);
  REQUIRE(result);
  return nlohmann::json::from_msgpack(buffer);
}
}  // namespace

TEST_SPAN_LINK("minimal link encodes only trace_id and span_id") {
  SpanLink link;
  link.trace_id = TraceID(0x1122334455667788ULL);  // high == 0
  link.span_id = 42;

  const auto j = encode_to_json(link);

  REQUIRE(j.is_object());
  REQUIRE(j.size() == 2);
  REQUIRE(j["trace_id"].get<std::uint64_t>() == 0x1122334455667788ULL);
  REQUIRE(j["span_id"].get<std::uint64_t>() == 42);
  REQUIRE_FALSE(j.contains("trace_id_high"));
  REQUIRE_FALSE(j.contains("attributes"));
  REQUIRE_FALSE(j.contains("tracestate"));
  REQUIRE_FALSE(j.contains("flags"));
}

TEST_SPAN_LINK("128-bit trace id emits trace_id_high") {
  SpanLink link;
  link.trace_id = TraceID(/*low=*/0xAAAAAAAAAAAAAAAAULL,
                          /*high=*/0xBBBBBBBBBBBBBBBBULL);
  link.span_id = 7;

  const auto j = encode_to_json(link);

  REQUIRE(j["trace_id"].get<std::uint64_t>() == 0xAAAAAAAAAAAAAAAAULL);
  REQUIRE(j["trace_id_high"].get<std::uint64_t>() == 0xBBBBBBBBBBBBBBBBULL);
  REQUIRE(j["span_id"].get<std::uint64_t>() == 7);
}

TEST_SPAN_LINK("attributes encode as a string map and omit when empty") {
  SpanLink link;
  link.trace_id = TraceID(1);
  link.span_id = 2;

  SECTION("present") {
    link.attributes = {{"link.key", "value"}, {"k2", "v2"}};
    const auto j = encode_to_json(link);
    REQUIRE(j["attributes"]["link.key"].get<std::string>() == "value");
    REQUIRE(j["attributes"]["k2"].get<std::string>() == "v2");
  }

  SECTION("empty -> omitted") {
    const auto j = encode_to_json(link);
    REQUIRE_FALSE(j.contains("attributes"));
  }
}

TEST_SPAN_LINK("tracestate omitted when empty, present otherwise") {
  SpanLink link;
  link.trace_id = TraceID(1);
  link.span_id = 2;

  SECTION("non-empty") {
    link.tracestate = "dd=s:1";
    const auto j = encode_to_json(link);
    REQUIRE(j["tracestate"].get<std::string>() == "dd=s:1");
  }

  SECTION("empty string -> omitted") {
    link.tracestate = "";
    const auto j = encode_to_json(link);
    REQUIRE_FALSE(j.contains("tracestate"));
  }

  SECTION("unset -> omitted") {
    const auto j = encode_to_json(link);
    REQUIRE_FALSE(j.contains("tracestate"));
  }
}

TEST_SPAN_LINK("flags set the high bit when present") {
  SpanLink link;
  link.trace_id = TraceID(1);
  link.span_id = 2;

  SECTION("sampled") {
    link.flags = 1u;
    const auto j = encode_to_json(link);
    REQUIRE(j["flags"].get<std::uint32_t>() == (1u | (1u << 31)));
  }

  SECTION("zero is still present with high bit") {
    link.flags = 0u;
    const auto j = encode_to_json(link);
    REQUIRE(j["flags"].get<std::uint32_t>() == (1u << 31));
  }

  SECTION("unset -> omitted") {
    const auto j = encode_to_json(link);
    REQUIRE_FALSE(j.contains("flags"));
  }
}

#include "span_data.h"  // internal header; available via test include dirs

TEST_SPAN_LINK("SpanData omits span_links when there are none") {
  SpanData span;
  std::string buffer;
  const auto result = msgpack_encode(buffer, span);
  REQUIRE(result);

  const auto j = nlohmann::json::from_msgpack(buffer);
  REQUIRE(j.is_object());
  REQUIRE_FALSE(j.contains("span_links"));
}

TEST_SPAN_LINK("SpanData emits span_links array when present") {
  SpanData span;

  SpanLink link;
  link.trace_id = TraceID(/*low=*/0x99, /*high=*/0x11);
  link.span_id = 123;
  link.attributes = {{"link.key", "value"}};
  span.span_links.push_back(link);

  std::string buffer;
  const auto result = msgpack_encode(buffer, span);
  REQUIRE(result);

  const auto j = nlohmann::json::from_msgpack(buffer);
  REQUIRE(j.contains("span_links"));
  REQUIRE(j["span_links"].is_array());
  REQUIRE(j["span_links"].size() == 1);

  const auto& encoded = j["span_links"][0];
  REQUIRE(encoded["trace_id"].get<std::uint64_t>() == 0x99);
  REQUIRE(encoded["trace_id_high"].get<std::uint64_t>() == 0x11);
  REQUIRE(encoded["span_id"].get<std::uint64_t>() == 123);
  REQUIRE(encoded["attributes"]["link.key"].get<std::string>() == "value");
}
