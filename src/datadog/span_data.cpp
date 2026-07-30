#include "span_data.h"

#include <datadog/error.h>
#include <datadog/span_config.h>
#include <datadog/span_defaults.h>
#include <datadog/string_view.h>

#include <cassert>
#include <cstddef>
#include <vector>

#include "msgpack.h"
#include "tags.h"

namespace datadog {
namespace tracing {
namespace {

Optional<StringView> lookup(
    const std::string& key,
    const std::unordered_map<std::string, std::string>& map) {
  const auto found = map.find(key);
  if (found != map.end()) {
    return found->second;
  }
  return nullopt;
}

}  // namespace

Optional<StringView> SpanData::environment() const {
  return lookup(tags::environment, tags);
}

Optional<StringView> SpanData::version() const {
  return lookup(tags::version, tags);
}

void SpanData::apply_config(const SpanDefaults& defaults,
                            const SpanConfig& config, const Clock& clock) {
  std::string version;
  if (config.service) {
    service = *config.service;
    version = config.version.value_or("");
  } else {
    service = defaults.service;
    version = defaults.version;
  }

  if (!version.empty()) {
    tags.insert_or_assign(tags::version, version);
  }

  name = config.name.value_or(defaults.name);

  for (const auto& item : defaults.tags) {
    tags.insert(item);
  }
  std::string environment = config.environment.value_or(defaults.environment);
  if (!environment.empty()) {
    tags.insert_or_assign(tags::environment, environment);
  }

  for (const auto& [key, value] : config.tags) {
    tags.insert_or_assign(key, value);
  }

  resource = config.resource.value_or(name);
  service_type = config.service_type.value_or(defaults.service_type);
  if (config.start) {
    start = *config.start;
  } else {
    start = clock();
  }
}

Expected<void> msgpack_encode(std::string& destination, const SpanData& span) {
  auto pack_service = [&](auto& destination) {
    return msgpack::pack_string(destination, span.service);
  };
  auto pack_name = [&](auto& destination) {
    return msgpack::pack_string(destination, span.name);
  };
  auto pack_resource = [&](auto& destination) {
    return msgpack::pack_string(destination, span.resource);
  };
  auto pack_trace_id = [&](auto& destination) {
    msgpack::pack_integer(destination, span.trace_id.low);
    return Expected<void>{};
  };
  auto pack_span_id = [&](auto& destination) {
    msgpack::pack_integer(destination, span.span_id);
    return Expected<void>{};
  };
  auto pack_parent_id = [&](auto& destination) {
    msgpack::pack_integer(destination, span.parent_id);
    return Expected<void>{};
  };
  auto pack_start = [&](auto& destination) {
    msgpack::pack_integer(
        destination,
        std::uint64_t(std::chrono::duration_cast<std::chrono::nanoseconds>(
                          span.start.wall.time_since_epoch())
                          .count()));
    return Expected<void>{};
  };
  auto pack_duration = [&](auto& destination) {
    msgpack::pack_integer(
        destination,
        std::uint64_t(
            std::chrono::duration_cast<std::chrono::nanoseconds>(span.duration)
                .count()));
    return Expected<void>{};
  };
  auto pack_error = [&](auto& destination) {
    msgpack::pack_integer(destination, std::int32_t(span.error));
    return Expected<void>{};
  };
  auto pack_meta = [&](auto& destination) {
    return msgpack::pack_map(destination, span.tags,
                             [](std::string& destination, const auto& value) {
                               return msgpack::pack_string(destination, value);
                             });
  };
  auto pack_metrics = [&](auto& destination) {
    return msgpack::pack_map(destination, span.numeric_tags,
                             [](std::string& destination, const auto& value) {
                               msgpack::pack_double(destination, value);
                               return Expected<void>{};
                             });
  };
  auto pack_type = [&](auto& destination) {
    return msgpack::pack_string(destination, span.service_type);
  };

  Expected<void> result =
      msgpack::pack_map(destination, span.span_links.empty() ? 12 : 13);
  if (!result) return result;

  result = msgpack::pack_map_suffix(
      destination, "service", pack_service, "name", pack_name, "resource",
      pack_resource, "trace_id", pack_trace_id, "span_id", pack_span_id,
      "parent_id", pack_parent_id, "start", pack_start, "duration",
      pack_duration, "error", pack_error, "meta", pack_meta, "metrics",
      pack_metrics, "type", pack_type);
  if (!result) return result;

  if (span.span_links.empty()) return result;

  auto pack_span_links = [&](auto& destination) {
    return msgpack::pack_array(
        destination, span.span_links,
        [](std::string& destination, const SpanLink& link) {
          return msgpack_encode(destination, link);
        });
  };
  return msgpack::pack_map_suffix(destination, "span_links", pack_span_links);
}

Expected<void> msgpack_encode(
    std::string& destination,
    const std::vector<std::unique_ptr<SpanData>>& spans) {
  return msgpack::pack_array(destination, spans,
                             [](auto& destination, const auto& span_ptr) {
                               assert(span_ptr);
                               return msgpack_encode(destination, *span_ptr);
                             });
}

}  // namespace tracing
}  // namespace datadog
