#include <chrono>
#include <memory>
#include <string>
#include <vector>

#include "mocks/http_clients.h"
#include "mocks/loggers.h"
#include "span_data.h"
#include "stats_concentrator.h"
#include "test.h"

using namespace datadog;
using namespace datadog::tracing;
using namespace std::chrono_literals;

#define STATS_TEST(x) TEST_CASE(x, "[stats_concentrator]")

namespace {

// Create a SpanData with reasonable defaults for testing.
std::unique_ptr<SpanData> make_span(
    const std::string& service, const std::string& name,
    const std::string& resource, bool is_error = false,
    std::uint64_t parent_id = 0,
    std::chrono::nanoseconds duration = 1000000ns) {
  auto span = std::make_unique<SpanData>();
  span->service = service;
  span->name = name;
  span->resource = resource;
  span->parent_id = parent_id;
  span->error = is_error;

  // Set start to a known time, e.g. 2024-01-01 00:00:00 UTC
  auto wall =
      std::chrono::system_clock::time_point(std::chrono::seconds(1704067200));
  auto tick = std::chrono::steady_clock::now();
  span->start = TimePoint{wall, tick};
  span->duration = duration;

  return span;
}

}  // namespace

STATS_TEST("is_top_level: parent_id == 0") {
  auto span = make_span("svc", "op", "res");
  span->parent_id = 0;

  CHECK(is_top_level(*span));
}

STATS_TEST("is_top_level: parent_id != 0 without tag") {
  auto span = make_span("svc", "op", "res");
  span->parent_id = 12345;

  CHECK(!is_top_level(*span));
}

STATS_TEST("is_top_level: _dd.top_level numeric tag == 1") {
  auto span = make_span("svc", "op", "res");
  span->parent_id = 12345;
  span->numeric_tags["_dd.top_level"] = 1.0;

  CHECK(is_top_level(*span));
}

STATS_TEST("is_measured: _dd.measured numeric tag == 1") {
  auto span = make_span("svc", "op", "res");
  span->parent_id = 12345;
  span->numeric_tags["_dd.measured"] = 1.0;

  CHECK(is_measured(*span));
}

STATS_TEST("is_measured: no tag") {
  auto span = make_span("svc", "op", "res");
  span->parent_id = 12345;

  CHECK(!is_measured(*span));
}

STATS_TEST("is_stats_eligible: top-level span is eligible") {
  auto span = make_span("svc", "op", "res");
  span->parent_id = 0;

  CHECK(is_stats_eligible(*span));
}

STATS_TEST("is_stats_eligible: measured span is eligible") {
  auto span = make_span("svc", "op", "res");
  span->parent_id = 12345;
  span->numeric_tags["_dd.measured"] = 1.0;

  CHECK(is_stats_eligible(*span));
}

STATS_TEST("is_stats_eligible: server span kind is eligible") {
  auto span = make_span("svc", "op", "res");
  span->parent_id = 12345;
  span->tags["span.kind"] = "server";

  CHECK(is_stats_eligible(*span));
}

STATS_TEST("is_stats_eligible: client span kind is eligible") {
  auto span = make_span("svc", "op", "res");
  span->parent_id = 12345;
  span->tags["span.kind"] = "client";

  CHECK(is_stats_eligible(*span));
}

STATS_TEST("is_stats_eligible: producer span kind is eligible") {
  auto span = make_span("svc", "op", "res");
  span->parent_id = 12345;
  span->tags["span.kind"] = "producer";

  CHECK(is_stats_eligible(*span));
}

STATS_TEST("is_stats_eligible: consumer span kind is eligible") {
  auto span = make_span("svc", "op", "res");
  span->parent_id = 12345;
  span->tags["span.kind"] = "consumer";

  CHECK(is_stats_eligible(*span));
}

STATS_TEST("is_stats_eligible: internal span without other tags is not") {
  auto span = make_span("svc", "op", "res");
  span->parent_id = 12345;
  span->tags["span.kind"] = "internal";

  CHECK(!is_stats_eligible(*span));
}

STATS_TEST("is_stats_eligible: span with no special tags and parent is not") {
  auto span = make_span("svc", "op", "res");
  span->parent_id = 12345;

  CHECK(!is_stats_eligible(*span));
}

STATS_TEST("extract_http_status_code: from string tag") {
  auto span = make_span("svc", "op", "res");
  span->tags["http.status_code"] = "200";

  CHECK(extract_http_status_code(*span) == 200);
}

STATS_TEST("extract_http_status_code: from numeric tag") {
  auto span = make_span("svc", "op", "res");
  span->numeric_tags["http.status_code"] = 404.0;

  CHECK(extract_http_status_code(*span) == 404);
}

STATS_TEST("extract_http_status_code: no tag") {
  auto span = make_span("svc", "op", "res");

  CHECK(extract_http_status_code(*span) == 0);
}

STATS_TEST("extract_grpc_status_code: rpc.grpc.status_code tag") {
  auto span = make_span("svc", "op", "res");
  span->tags["rpc.grpc.status_code"] = "2";

  CHECK(extract_grpc_status_code(*span) == "2");
}

STATS_TEST("extract_grpc_status_code: grpc.code numeric tag") {
  auto span = make_span("svc", "op", "res");
  span->numeric_tags["grpc.code"] = 14.0;

  CHECK(extract_grpc_status_code(*span) == "14");
}

STATS_TEST("extract_grpc_status_code: no tag") {
  auto span = make_span("svc", "op", "res");

  CHECK(extract_grpc_status_code(*span) == "");
}

STATS_TEST("concentrator: add eligible span creates bucket") {
  auto http_client = std::make_shared<MockHTTPClient>();
  auto logger = std::make_shared<MockLogger>();
  HTTPClient::URL agent_url;
  agent_url.scheme = "http";
  agent_url.authority = "localhost:8126";
  agent_url.path = "";

  StatsConcentrator concentrator(http_client, agent_url, logger, "host1",
                                 "prod", "1.0", "svc", "cpp");

  auto span = make_span("svc", "web.request", "/api/test");
  concentrator.add(*span);

  CHECK(concentrator.bucket_count() == 1);
}

STATS_TEST("concentrator: add ineligible span does not create bucket") {
  auto http_client = std::make_shared<MockHTTPClient>();
  auto logger = std::make_shared<MockLogger>();
  HTTPClient::URL agent_url;
  agent_url.scheme = "http";
  agent_url.authority = "localhost:8126";
  agent_url.path = "";

  StatsConcentrator concentrator(http_client, agent_url, logger, "host1",
                                 "prod", "1.0", "svc", "cpp");

  auto span = make_span("svc", "internal.op", "/internal");
  span->parent_id = 12345;  // not top-level
  // No measured tag, no special span.kind
  concentrator.add(*span);

  CHECK(concentrator.bucket_count() == 0);
}

STATS_TEST("concentrator: multiple spans in same bucket") {
  auto http_client = std::make_shared<MockHTTPClient>();
  auto logger = std::make_shared<MockLogger>();
  HTTPClient::URL agent_url;
  agent_url.scheme = "http";
  agent_url.authority = "localhost:8126";
  agent_url.path = "";

  StatsConcentrator concentrator(http_client, agent_url, logger, "host1",
                                 "prod", "1.0", "svc", "cpp");

  // Create two spans with the same start time (same 10s bucket).
  auto span1 = make_span("svc", "web.request", "/api/test", false, 0, 1ms);
  auto span2 = make_span("svc", "web.request", "/api/test", true, 0, 2ms);

  concentrator.add(*span1);
  concentrator.add(*span2);

  // Both should be in the same bucket.
  CHECK(concentrator.bucket_count() == 1);
}

STATS_TEST("concentrator: spans in different buckets") {
  auto http_client = std::make_shared<MockHTTPClient>();
  auto logger = std::make_shared<MockLogger>();
  HTTPClient::URL agent_url;
  agent_url.scheme = "http";
  agent_url.authority = "localhost:8126";
  agent_url.path = "";

  StatsConcentrator concentrator(http_client, agent_url, logger, "host1",
                                 "prod", "1.0", "svc", "cpp");

  auto span1 = make_span("svc", "web.request", "/api/test", false, 0, 1ms);

  // Create a span with a different start time (20 seconds later).
  auto span2 = make_span("svc", "web.request", "/api/other", false, 0, 1ms);
  span2->start.wall += std::chrono::seconds(20);

  concentrator.add(*span1);
  concentrator.add(*span2);

  CHECK(concentrator.bucket_count() == 2);
}

STATS_TEST("concentrator: encode_payload produces non-empty output") {
  auto http_client = std::make_shared<MockHTTPClient>();
  auto logger = std::make_shared<MockLogger>();
  HTTPClient::URL agent_url;
  agent_url.scheme = "http";
  agent_url.authority = "localhost:8126";
  agent_url.path = "";

  StatsConcentrator concentrator(http_client, agent_url, logger, "host1",
                                 "prod", "1.0", "svc", "cpp");

  StatsBucket bucket;
  bucket.start_ns = 1704067200000000000ULL;
  bucket.duration_ns = 10000000000ULL;

  StatsAggregationKey key;
  key.service = "svc";
  key.name = "web.request";
  key.resource = "/api/test";
  key.type = "web";
  key.is_trace_root = Trilean::TRUE;

  StatsGroupData group;
  group.hits = 5;
  group.errors = 1;
  group.duration = 50000000;          // 50ms total
  group.ok_sketch.add(10000000.0);    // 10ms
  group.error_sketch.add(5000000.0);  // 5ms

  bucket.groups[key] = std::move(group);

  std::vector<StatsBucket> buckets = {std::move(bucket)};
  std::string payload = concentrator.encode_payload(buckets);

  CHECK(!payload.empty());
  // Should start with msgpack MAP32 marker.
  CHECK(static_cast<unsigned char>(payload[0]) == 0xDF);
}

STATS_TEST("StatsAggregationKey equality") {
  StatsAggregationKey a;
  a.service = "svc";
  a.name = "op";
  a.resource = "res";
  a.type = "web";
  a.http_status_code = 200;
  a.is_trace_root = Trilean::TRUE;

  StatsAggregationKey b = a;
  CHECK(a == b);

  b.service = "other-svc";
  CHECK(!(a == b));
}

STATS_TEST("StatsAggregationKey hash: equal keys same hash") {
  StatsAggregationKey a;
  a.service = "svc";
  a.name = "op";
  a.resource = "res";
  a.type = "web";

  StatsAggregationKey b = a;

  StatsAggregationKeyHash hasher;
  CHECK(hasher(a) == hasher(b));
}

STATS_TEST("StatsAggregationKey hash: different keys different hash") {
  StatsAggregationKey a;
  a.service = "svc-a";
  a.name = "op";

  StatsAggregationKey b;
  b.service = "svc-b";
  b.name = "op";

  StatsAggregationKeyHash hasher;
  // This could theoretically collide, but with these inputs it should not.
  CHECK(hasher(a) != hasher(b));
}
