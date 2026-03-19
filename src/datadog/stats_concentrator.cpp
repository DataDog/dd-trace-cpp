#include "stats_concentrator.h"

#include <datadog/dict_writer.h>
#include <datadog/error.h>
#include <datadog/logger.h>
#include <datadog/string_view.h>

#include <algorithm>
#include <cassert>
#include <charconv>
#include <chrono>
#include <functional>
#include <string>

#include "msgpack.h"
#include "span_data.h"
#include "string_util.h"

namespace datadog {
namespace tracing {
namespace {

constexpr StringView stats_api_path = "/v0.6/stats";

constexpr uint64_t bucket_duration_ns = static_cast<uint64_t>(
    std::chrono::duration_cast<std::chrono::nanoseconds>(stats_bucket_duration)
        .count());

// Tags used for span eligibility and stats dimensions.
constexpr StringView tag_measured = "_dd.measured";
constexpr StringView tag_top_level = "_dd.top_level";
constexpr StringView tag_span_kind = "span.kind";
constexpr StringView tag_http_status_code = "http.status_code";
constexpr StringView tag_http_method = "http.method";
constexpr StringView tag_http_endpoint = "http.endpoint";
constexpr StringView tag_origin = "_dd.origin";
constexpr StringView tag_base_service = "_dd.base_service";

// gRPC status code tag candidates (checked in order of precedence).
constexpr StringView grpc_tag_candidates[] = {
    "rpc.grpc.status_code",
    "grpc.code",
    "rpc.grpc.status.code",
    "grpc.status.code",
};

// Peer tag keys to extract for client/producer/consumer spans.
constexpr StringView peer_tag_keys[] = {
    "peer.service",
    "db.instance",
    "db.system",
    "peer.hostname",
    "net.peer.name",
    "server.address",
    "network.destination.name",
    "messaging.destination",
    "messaging.destination.name",
    "messaging.kafka.bootstrap.servers",
    "rpc.service",
    "aws.queue.name",
    "aws.s3.bucket",
    "aws.sqs.queue_name",
    "aws.kinesis.stream",
    "aws.dynamodb.table",
    "topicname",
    "out.host",
};

// Span kinds that make a span eligible for stats.
bool is_stats_span_kind(StringView kind) {
  return kind == "server" || kind == "client" || kind == "producer" ||
         kind == "consumer";
}

// Span kinds that should include peer tags.
bool is_peer_tag_span_kind(StringView kind) {
  return kind == "client" || kind == "producer" || kind == "consumer";
}

Optional<StringView> lookup_tag(
    const std::unordered_map<std::string, std::string>& tags, StringView key) {
  auto it = tags.find(std::string(key));
  if (it != tags.end()) {
    return StringView(it->second);
  }
  return nullopt;
}

Optional<double> lookup_numeric_tag(
    const std::unordered_map<std::string, double>& tags, StringView key) {
  auto it = tags.find(std::string(key));
  if (it != tags.end()) {
    return it->second;
  }
  return nullopt;
}

uint32_t parse_status_code(StringView value) {
  uint32_t result = 0;
  auto [ptr, ec] =
      std::from_chars(value.data(), value.data() + value.size(), result);
  if (ec != std::errc()) {
    return 0;
  }
  return result;
}

HTTPClient::URL stats_endpoint(const HTTPClient::URL& agent_url) {
  auto url = agent_url;
  append(url.path, stats_api_path);
  return url;
}

// Combine hash values (boost-style).
inline void hash_combine(std::size_t& seed, std::size_t value) {
  seed ^= value + 0x9e3779b9 + (seed << 6) + (seed >> 2);
}

}  // namespace

// -- StatsAggregationKey --

bool StatsAggregationKey::operator==(const StatsAggregationKey& other) const {
  return service == other.service && name == other.name &&
         resource == other.resource && type == other.type &&
         http_status_code == other.http_status_code &&
         grpc_status_code == other.grpc_status_code &&
         span_kind == other.span_kind && synthetics == other.synthetics &&
         is_trace_root == other.is_trace_root &&
         peer_tags_hash == other.peer_tags_hash &&
         http_method == other.http_method &&
         http_endpoint == other.http_endpoint;
}

std::size_t StatsAggregationKeyHash::operator()(
    const StatsAggregationKey& key) const {
  std::size_t seed = 0;
  std::hash<std::string> hasher;
  std::hash<std::uint32_t> uint_hasher;
  std::hash<bool> bool_hasher;

  hash_combine(seed, hasher(key.service));
  hash_combine(seed, hasher(key.name));
  hash_combine(seed, hasher(key.resource));
  hash_combine(seed, hasher(key.type));
  hash_combine(seed, uint_hasher(key.http_status_code));
  hash_combine(seed, uint_hasher(key.grpc_status_code));
  hash_combine(seed, hasher(key.span_kind));
  hash_combine(seed, bool_hasher(key.synthetics));
  hash_combine(seed, uint_hasher(static_cast<uint32_t>(key.is_trace_root)));
  hash_combine(seed, hasher(key.peer_tags_hash));
  hash_combine(seed, hasher(key.http_method));
  hash_combine(seed, hasher(key.http_endpoint));
  return seed;
}

// -- Free functions --

bool is_top_level(const SpanData& span) {
  // A span is top-level if parent_id == 0, or if _dd.top_level == 1.
  if (span.parent_id == 0) {
    return true;
  }
  auto it = span.numeric_tags.find(std::string(tag_top_level));
  return it != span.numeric_tags.end() && it->second == 1.0;
}

bool is_measured(const SpanData& span) {
  auto it = span.numeric_tags.find(std::string(tag_measured));
  return it != span.numeric_tags.end() && it->second == 1.0;
}

bool is_stats_eligible(const SpanData& span) {
  if (is_top_level(span) || is_measured(span)) {
    return true;
  }
  auto kind = lookup_tag(span.tags, tag_span_kind);
  if (kind && is_stats_span_kind(*kind)) {
    return true;
  }
  return false;
}

std::uint32_t extract_grpc_status_code(const SpanData& span) {
  for (const auto& tag_name : grpc_tag_candidates) {
    // Check string tags first.
    auto str_val = lookup_tag(span.tags, tag_name);
    if (str_val) {
      uint32_t code = parse_status_code(*str_val);
      if (code > 0) return code;
    }
    // Check numeric tags.
    auto num_val = lookup_numeric_tag(span.numeric_tags, tag_name);
    if (num_val) {
      return static_cast<uint32_t>(*num_val);
    }
  }
  return 0;
}

std::uint32_t extract_http_status_code(const SpanData& span) {
  auto val = lookup_tag(span.tags, tag_http_status_code);
  if (val) {
    return parse_status_code(*val);
  }
  auto num_val = lookup_numeric_tag(span.numeric_tags, tag_http_status_code);
  if (num_val) {
    return static_cast<uint32_t>(*num_val);
  }
  return 0;
}

// -- StatsConcentrator --

StatsConcentrator::StatsConcentrator(
    const std::shared_ptr<HTTPClient>& http_client,
    const HTTPClient::URL& agent_url, const std::shared_ptr<Logger>& logger,
    std::string hostname, std::string env, std::string version,
    std::string service, std::string lang)
    : http_client_(http_client),
      stats_endpoint_(stats_endpoint(agent_url)),
      logger_(logger),
      hostname_(std::move(hostname)),
      env_(std::move(env)),
      version_(std::move(version)),
      service_(std::move(service)),
      lang_(std::move(lang)) {}

void StatsConcentrator::add(const SpanData& span) {
  if (!is_stats_eligible(span)) {
    return;
  }

  // Calculate span end time in nanoseconds.
  auto start_ns = static_cast<uint64_t>(
      std::chrono::duration_cast<std::chrono::nanoseconds>(
          span.start.wall.time_since_epoch())
          .count());
  auto dur_ns = static_cast<uint64_t>(
      std::chrono::duration_cast<std::chrono::nanoseconds>(span.duration)
          .count());
  uint64_t end_ns = start_ns + dur_ns;

  uint64_t bucket_start = align_timestamp(end_ns);

  auto key = make_key(span);
  std::string peer_tags = extract_peer_tags(span);

  std::lock_guard<std::mutex> lock(mutex_);

  auto& bucket = get_or_create_bucket(bucket_start);
  auto& group = bucket.groups[key];

  group.hits++;
  group.duration += dur_ns;

  if (span.error) {
    group.errors++;
    group.error_sketch.add(static_cast<double>(dur_ns));
  } else {
    group.ok_sketch.add(static_cast<double>(dur_ns));
  }

  if (group.peer_tags_serialized.empty() && !peer_tags.empty()) {
    group.peer_tags_serialized = std::move(peer_tags);
  }
}

void StatsConcentrator::flush(TimePoint now) {
  auto now_ns = static_cast<uint64_t>(
      std::chrono::duration_cast<std::chrono::nanoseconds>(
          now.wall.time_since_epoch())
          .count());

  std::vector<StatsBucket> to_flush;

  {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = buckets_.begin();
    while (it != buckets_.end()) {
      // Flush buckets whose end time is before now.
      if (it->second.start_ns + it->second.duration_ns <= now_ns) {
        to_flush.push_back(std::move(it->second));
        it = buckets_.erase(it);
      } else {
        ++it;
      }
    }
  }

  if (to_flush.empty()) {
    return;
  }

  std::string body = encode_payload(to_flush);

  auto set_headers = [](DictWriter& writer) {
    writer.set("Content-Type", "application/msgpack");
    writer.set("Datadog-Client-Computed-Stats", "yes");
    writer.set("Datadog-Client-Computed-Top-Level", "yes");
  };

  auto on_response = [logger = logger_](int status,
                                        const DictReader& /*headers*/,
                                        std::string response_body) {
    if (status < 200 || status >= 300) {
      logger->log_error([&](auto& stream) {
        stream << "Stats flush: unexpected response status " << status
               << " with body: " << response_body;
      });
    }
    // Fire-and-forget: we don't process the response body.
  };

  auto on_error = [logger = logger_](Error error) {
    logger->log_error(
        error.with_prefix("Error during stats flush HTTP request: "));
  };

  // Use the clock for deadline. 2-second timeout.
  auto deadline =
      std::chrono::steady_clock::now() + std::chrono::milliseconds(2000);

  auto result = http_client_->post(stats_endpoint_, std::move(set_headers),
                                   std::move(body), std::move(on_response),
                                   std::move(on_error), deadline);
  if (auto* error = result.if_error()) {
    logger_->log_error(
        error->with_prefix("Unexpected error submitting stats: "));
  }
}

void StatsConcentrator::flush_all() {
  std::vector<StatsBucket> to_flush;

  {
    std::lock_guard<std::mutex> lock(mutex_);
    to_flush.reserve(buckets_.size());
    for (auto& [ts, bucket] : buckets_) {
      to_flush.push_back(std::move(bucket));
    }
    buckets_.clear();
  }

  if (to_flush.empty()) {
    return;
  }

  std::string body = encode_payload(to_flush);

  auto set_headers = [](DictWriter& writer) {
    writer.set("Content-Type", "application/msgpack");
    writer.set("Datadog-Client-Computed-Stats", "yes");
    writer.set("Datadog-Client-Computed-Top-Level", "yes");
  };

  auto on_response = [logger = logger_](int status,
                                        const DictReader& /*headers*/,
                                        std::string response_body) {
    if (status < 200 || status >= 300) {
      logger->log_error([&](auto& stream) {
        stream << "Stats flush_all: unexpected response status " << status
               << " with body: " << response_body;
      });
    }
  };

  auto on_error = [logger = logger_](Error error) {
    logger->log_error(
        error.with_prefix("Error during stats flush_all HTTP request: "));
  };

  auto deadline =
      std::chrono::steady_clock::now() + std::chrono::milliseconds(2000);

  auto result = http_client_->post(stats_endpoint_, std::move(set_headers),
                                   std::move(body), std::move(on_response),
                                   std::move(on_error), deadline);
  if (auto* error = result.if_error()) {
    logger_->log_error(
        error->with_prefix("Unexpected error submitting final stats: "));
  }
}

std::size_t StatsConcentrator::bucket_count() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return buckets_.size();
}

uint64_t StatsConcentrator::align_timestamp(uint64_t end_time_ns) {
  // Align to 10-second boundary.
  return (end_time_ns / bucket_duration_ns) * bucket_duration_ns;
}

StatsBucket& StatsConcentrator::get_or_create_bucket(uint64_t bucket_start_ns) {
  auto it = buckets_.find(bucket_start_ns);
  if (it != buckets_.end()) {
    return it->second;
  }
  auto& bucket = buckets_[bucket_start_ns];
  bucket.start_ns = bucket_start_ns;
  bucket.duration_ns = bucket_duration_ns;
  return bucket;
}

StatsAggregationKey StatsConcentrator::make_key(const SpanData& span) {
  StatsAggregationKey key;

  key.service = span.service;
  key.name = span.name;
  key.resource = span.resource;
  key.type = span.service_type;

  key.http_status_code = extract_http_status_code(span);
  key.grpc_status_code = extract_grpc_status_code(span);

  auto kind = lookup_tag(span.tags, tag_span_kind);
  if (kind) {
    key.span_kind = std::string(*kind);
  }

  // Synthetics detection: _dd.origin == "synthetics" or
  // _dd.origin starts with "synthetics-"
  auto origin = lookup_tag(span.tags, tag_origin);
  if (origin) {
    key.synthetics =
        (*origin == "synthetics" || starts_with(*origin, "synthetics-"));
  }

  // is_trace_root: TRUE if parent_id == 0, FALSE otherwise.
  key.is_trace_root = (span.parent_id == 0) ? Trilean::TRUE : Trilean::FALSE;

  key.peer_tags_hash = extract_peer_tags(span);

  auto http_method = lookup_tag(span.tags, tag_http_method);
  if (http_method) {
    key.http_method = std::string(*http_method);
  }

  auto http_endpoint = lookup_tag(span.tags, tag_http_endpoint);
  if (http_endpoint) {
    key.http_endpoint = std::string(*http_endpoint);
  }

  return key;
}

std::string StatsConcentrator::extract_peer_tags(const SpanData& span) {
  auto kind = lookup_tag(span.tags, tag_span_kind);
  bool should_extract = false;

  if (kind && is_peer_tag_span_kind(*kind)) {
    should_extract = true;
  }

  // Internal span with _dd.base_service override.
  if (!should_extract) {
    auto base_service = lookup_tag(span.tags, tag_base_service);
    if (base_service && *base_service != span.service) {
      should_extract = true;
    }
  }

  if (!should_extract) {
    return "";
  }

  // Build a sorted, comma-separated list of peer_tag=value pairs.
  std::vector<std::pair<std::string, std::string>> pairs;
  for (const auto& tag_key : peer_tag_keys) {
    auto val = lookup_tag(span.tags, tag_key);
    if (val) {
      pairs.emplace_back(std::string(tag_key), std::string(*val));
    }
  }

  if (pairs.empty()) {
    return "";
  }

  std::sort(pairs.begin(), pairs.end());

  std::string result;
  for (std::size_t i = 0; i < pairs.size(); ++i) {
    if (i > 0) result += ',';
    result += pairs[i].first;
    result += '=';
    result += pairs[i].second;
  }
  return result;
}

std::string StatsConcentrator::encode_payload(
    const std::vector<StatsBucket>& buckets) const {
  std::string payload;

  // The /v0.6/stats payload is a msgpack map:
  // {
  //   "Hostname": string,
  //   "Env": string,
  //   "Version": string,
  //   "Lang": string,
  //   "Stats": [
  //     {
  //       "Start": uint64 (bucket start in ns),
  //       "Duration": uint64 (bucket duration in ns),
  //       "Stats": [
  //         {
  //           "Service": string,
  //           "Name": string,
  //           "Resource": string,
  //           "Type": string,
  //           "HTTPStatusCode": uint32,
  //           "Hits": uint64,
  //           "Errors": uint64,
  //           "Duration": uint64,
  //           "OkSummary": DDSketch,
  //           "ErrorSummary": DDSketch,
  //           "SpanKind": string,
  //           "Synthetics": bool,
  //           "IsTraceRoot": Trilean,
  //           "PeerTags": string (serialized),
  //           "GRPCStatusCode": uint32,
  //           "HTTPMethod": string,
  //           "HTTPEndpoint": string,
  //         }, ...
  //       ]
  //     }, ...
  //   ]
  // }

  // clang-format off
  // Top-level map with 5 keys.
  msgpack::pack_map(payload, 5);

  msgpack::pack_string(payload, "Hostname");
  msgpack::pack_string(payload, hostname_);

  msgpack::pack_string(payload, "Env");
  msgpack::pack_string(payload, env_);

  msgpack::pack_string(payload, "Version");
  msgpack::pack_string(payload, version_);

  msgpack::pack_string(payload, "Lang");
  msgpack::pack_string(payload, lang_);

  msgpack::pack_string(payload, "Stats");
  msgpack::pack_array(payload, buckets.size());

  for (const auto& bucket : buckets) {
    // Each bucket has 3 keys: Start, Duration, Stats.
    msgpack::pack_map(payload, 3);

    msgpack::pack_string(payload, "Start");
    msgpack::pack_integer(payload, static_cast<std::uint64_t>(bucket.start_ns));

    msgpack::pack_string(payload, "Duration");
    msgpack::pack_integer(payload, static_cast<std::uint64_t>(bucket.duration_ns));

    msgpack::pack_string(payload, "Stats");
    msgpack::pack_array(payload, bucket.groups.size());

    for (const auto& [agg_key, group] : bucket.groups) {
      // Each stats group has up to 17 keys.
      msgpack::pack_map(payload, 17);

      msgpack::pack_string(payload, "Service");
      msgpack::pack_string(payload, agg_key.service);

      msgpack::pack_string(payload, "Name");
      msgpack::pack_string(payload, agg_key.name);

      msgpack::pack_string(payload, "Resource");
      msgpack::pack_string(payload, agg_key.resource);

      msgpack::pack_string(payload, "Type");
      msgpack::pack_string(payload, agg_key.type);

      msgpack::pack_string(payload, "HTTPStatusCode");
      msgpack::pack_integer(payload, static_cast<std::uint64_t>(agg_key.http_status_code));

      msgpack::pack_string(payload, "Hits");
      msgpack::pack_integer(payload, group.hits);

      msgpack::pack_string(payload, "Errors");
      msgpack::pack_integer(payload, group.errors);

      msgpack::pack_string(payload, "Duration");
      msgpack::pack_integer(payload, group.duration);

      msgpack::pack_string(payload, "OkSummary");
      group.ok_sketch.msgpack_encode(payload);

      msgpack::pack_string(payload, "ErrorSummary");
      group.error_sketch.msgpack_encode(payload);

      msgpack::pack_string(payload, "SpanKind");
      msgpack::pack_string(payload, agg_key.span_kind);

      msgpack::pack_string(payload, "Synthetics");
      msgpack::pack_integer(payload, std::int64_t(agg_key.synthetics ? 1 : 0));

      msgpack::pack_string(payload, "IsTraceRoot");
      msgpack::pack_integer(payload, std::int64_t(static_cast<uint32_t>(agg_key.is_trace_root)));

      msgpack::pack_string(payload, "PeerTags");
      msgpack::pack_string(payload, group.peer_tags_serialized);

      msgpack::pack_string(payload, "GRPCStatusCode");
      msgpack::pack_integer(payload, static_cast<std::uint64_t>(agg_key.grpc_status_code));

      msgpack::pack_string(payload, "HTTPMethod");
      msgpack::pack_string(payload, agg_key.http_method);

      msgpack::pack_string(payload, "HTTPEndpoint");
      msgpack::pack_string(payload, agg_key.http_endpoint);
    }
  }
  // clang-format on

  return payload;
}

}  // namespace tracing
}  // namespace datadog
