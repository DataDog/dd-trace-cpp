#pragma once

// This component provides the `StatsConcentrator` class, which aggregates span
// metrics into 10-second time buckets for the Client-Side Stats Computation
// (CSS) feature.
//
// When CSS is enabled, the tracer computes trace metrics (hit counts, error
// counts, duration distributions) locally rather than relying on the Datadog
// Agent.  This allows the Agent to drop priority-0 (unsampled) traces without
// losing visibility into service-level metrics.
//
// Each finished span that is "eligible" (top-level, measured, or has a
// span.kind of server/client/producer/consumer) is added to the concentrator.
// The concentrator groups spans by a 12-dimension aggregation key and
// accumulates counts and DDSketch distributions in 10-second time buckets.
//
// Periodically (at flush time), the concentrator serializes the accumulated
// stats and posts them to the Datadog Agent at POST /v0.6/stats.

#include <datadog/clock.h>
#include <datadog/expected.h>
#include <datadog/http_client.h>

#include <chrono>
#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

#include "ddsketch.h"

namespace datadog {
namespace tracing {

struct SpanData;
class Logger;

// The time bucket duration: 10 seconds.
constexpr auto stats_bucket_duration = std::chrono::seconds(10);

// Trilean: used for is_trace_root dimension.
enum class Trilean : std::uint32_t {
  NOT_SET = 0,
  TRUE = 1,
  FALSE = 2,
};

// The 12-dimension aggregation key.
struct StatsAggregationKey {
  std::string service;
  std::string name;  // operation name
  std::string resource;
  std::string type;
  std::uint32_t http_status_code = 0;
  std::uint32_t grpc_status_code = 0;
  std::string span_kind;
  bool synthetics = false;
  Trilean is_trace_root = Trilean::NOT_SET;
  std::string peer_tags_hash;  // serialized peer tags for hashing
  std::string http_method;
  std::string http_endpoint;

  bool operator==(const StatsAggregationKey& other) const;
};

// Hash function for StatsAggregationKey.
struct StatsAggregationKeyHash {
  std::size_t operator()(const StatsAggregationKey& key) const;
};

// The stats for a single aggregation key within a single time bucket.
struct StatsGroupData {
  std::uint64_t hits = 0;
  std::uint64_t errors = 0;
  std::uint64_t duration = 0;  // total duration in nanoseconds
  DDSketch ok_sketch{0.01, 2048};
  DDSketch error_sketch{0.01, 2048};
  std::string peer_tags_serialized;  // the actual peer tags string for payload
};

// A single 10-second time bucket.
struct StatsBucket {
  // The start time of this bucket (as a Unix timestamp in nanoseconds,
  // aligned to a 10-second boundary).
  std::uint64_t start_ns = 0;
  // Duration of the bucket in nanoseconds (always 10 seconds).
  std::uint64_t duration_ns = 0;

  std::unordered_map<StatsAggregationKey, StatsGroupData,
                     StatsAggregationKeyHash>
      groups;
};

// Return whether the specified `span` is eligible for stats computation.
// Eligible means: top-level OR measured OR span_kind in
// {server,client,producer,consumer}.
bool is_stats_eligible(const SpanData& span);

// Return whether the specified `span` is top-level (no parent, or
// _dd.top_level == 1).
bool is_top_level(const SpanData& span);

// Return whether the specified `span` is measured (_dd.measured == 1).
bool is_measured(const SpanData& span);

// Extract the gRPC status code from span tags, checking multiple tag names.
std::uint32_t extract_grpc_status_code(const SpanData& span);

// Extract the HTTP status code from span tags.
std::uint32_t extract_http_status_code(const SpanData& span);

class StatsConcentrator {
 public:
  // Construct a concentrator that will use the specified `http_client` to post
  // stats to the agent at the specified `agent_url`, with the specified
  // `logger` for diagnostics, and the specified `hostname`, `env`, and
  // `version` for the stats payload metadata.
  StatsConcentrator(const std::shared_ptr<HTTPClient>& http_client,
                    const HTTPClient::URL& agent_url,
                    const std::shared_ptr<Logger>& logger, std::string hostname,
                    std::string env, std::string version, std::string service,
                    std::string lang = "cpp");

  // Add the specified `span` to the concentrator.  The span must be eligible
  // (the caller should check `is_stats_eligible` first, though this method
  // will check again).
  void add(const SpanData& span);

  // Flush all complete buckets (those whose end time is before `now`) and
  // POST them to the agent.  This is typically called from the periodic flush
  // timer.
  void flush(TimePoint now);

  // Flush all buckets (including the current one). Called at shutdown.
  void flush_all();

  // Return the number of currently accumulated buckets (for testing).
  std::size_t bucket_count() const;

  // Encode the specified `buckets` into msgpack for POST /v0.6/stats.
  std::string encode_payload(const std::vector<StatsBucket>& buckets) const;

 private:
  // Return the bucket start time (aligned to 10-second boundary) for the
  // specified span end time.
  static std::uint64_t align_timestamp(std::uint64_t end_time_ns);

  // Get or create the bucket for the specified aligned start time.
  StatsBucket& get_or_create_bucket(std::uint64_t bucket_start_ns);

  // Build the StatsAggregationKey from a span.
  static StatsAggregationKey make_key(const SpanData& span);

  // Extract peer tags from a span (for client/producer/consumer kinds, or
  // internal with _dd.base_service override).
  static std::string extract_peer_tags(const SpanData& span);

  mutable std::mutex mutex_;
  std::shared_ptr<HTTPClient> http_client_;
  HTTPClient::URL stats_endpoint_;
  std::shared_ptr<Logger> logger_;

  std::string hostname_;
  std::string env_;
  std::string version_;
  std::string service_;
  std::string lang_;

  // Buckets keyed by their aligned start timestamp (nanoseconds).
  std::unordered_map<std::uint64_t, StatsBucket> buckets_;
};

}  // namespace tracing
}  // namespace datadog
