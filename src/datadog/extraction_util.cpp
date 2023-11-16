#include "extraction_util.h"

#include <cstdint>
#include <sstream>
#include <string>
#include <unordered_map>

#include "extracted_data.h"
#include "json.hpp"
#include "logger.h"
#include "parse_util.h"
#include "tag_propagation.h"
#include "tags.h"

namespace datadog {
namespace tracing {

// Parse the high 64 bits of a trace ID from the specified `value`. If `value`
// is correctly formatted, then return the resulting bits. If `value` is
// incorrectly formatted then return `nullopt`.
Optional<std::uint64_t> parse_trace_id_high(const std::string& value) {
  if (value.size() != 16) {
    return nullopt;
  }

  auto high = parse_uint64(value, 16);
  if (high) {
    return *high;
  }

  return nullopt;
}

// Decode the specified `trace_tags` and integrate them into the specified
// `result`. If an error occurs, add a `tags::internal::propagation_error` tag
// to the specified `span_tags` and log a diagnostic using the specified
// `logger`.
void handle_trace_tags(StringView trace_tags, ExtractedData& result,
                       std::unordered_map<std::string, std::string>& span_tags,
                       Logger& logger) {
  auto maybe_trace_tags = decode_tags(trace_tags);
  if (auto* error = maybe_trace_tags.if_error()) {
    logger.log_error(*error);
    span_tags[tags::internal::propagation_error] = "decoding_error";
    return;
  }

  for (auto& [key, value] : *maybe_trace_tags) {
    if (!starts_with(key, "_dd.p.")) {
      continue;
    }

    if (key == tags::internal::trace_id_high) {
      // _dd.p.tid contains the high 64 bits of the trace ID.
      const Optional<std::uint64_t> high = parse_trace_id_high(value);
      if (!high) {
        span_tags[tags::internal::propagation_error] = "malformed_tid " + value;
        continue;
      }

      if (result.trace_id) {
        // Note that this assumes the lower 64 bits of the trace ID have already
        // been extracted (i.e. we look for X-Datadog-Trace-ID first).
        result.trace_id->high = *high;
      }
    }

    result.trace_tags.emplace_back(std::move(key), std::move(value));
  }
}

Expected<Optional<std::uint64_t>> extract_id_header(const DictReader& headers,
                                                    StringView header,
                                                    StringView header_kind,
                                                    StringView style_name,
                                                    int base) {
  auto found = headers.lookup(header);
  if (!found) {
    return nullopt;
  }
  auto result = parse_uint64(*found, base);
  if (auto* error = result.if_error()) {
    std::string prefix;
    prefix += "Could not extract ";
    append(prefix, style_name);
    prefix += "-style ";
    append(prefix, header_kind);
    prefix += "ID from ";
    append(prefix, header);
    prefix += ": ";
    append(prefix, *found);
    prefix += ' ';
    return error->with_prefix(prefix);
  }
  return *result;
}

Expected<ExtractedData> extract_datadog(
    const DictReader& headers,
    std::unordered_map<std::string, std::string>& span_tags, Logger& logger) {
  ExtractedData result;
  result.style = PropagationStyle::DATADOG;

  auto trace_id =
      extract_id_header(headers, "x-datadog-trace-id", "trace", "Datadog", 10);
  if (auto* error = trace_id.if_error()) {
    return std::move(*error);
  }
  if (*trace_id) {
    result.trace_id = TraceID(**trace_id);
  }

  auto parent_id = extract_id_header(headers, "x-datadog-parent-id",
                                     "parent span", "Datadog", 10);
  if (auto* error = parent_id.if_error()) {
    return std::move(*error);
  }
  result.parent_id = *parent_id;

  const StringView sampling_priority_header = "x-datadog-sampling-priority";
  if (auto found = headers.lookup(sampling_priority_header)) {
    auto sampling_priority = parse_int(*found, 10);
    if (auto* error = sampling_priority.if_error()) {
      std::string prefix;
      prefix += "Could not extract Datadog-style sampling priority from ";
      append(prefix, sampling_priority_header);
      prefix += ": ";
      append(prefix, *found);
      prefix += ' ';
      return error->with_prefix(prefix);
    }
    result.sampling_priority = *sampling_priority;
  }

  auto origin = headers.lookup("x-datadog-origin");
  if (origin) {
    result.origin = std::string(*origin);
  }

  auto trace_tags = headers.lookup("x-datadog-tags");
  if (trace_tags) {
    handle_trace_tags(*trace_tags, result, span_tags, logger);
  }

  return result;
}

Expected<ExtractedData> extract_b3(
    const DictReader& headers, std::unordered_map<std::string, std::string>&,
    Logger&) {
  ExtractedData result;
  result.style = PropagationStyle::B3;

  if (auto found = headers.lookup("x-b3-traceid")) {
    auto parsed = TraceID::parse_hex(*found);
    if (auto* error = parsed.if_error()) {
      std::string prefix = "Could not extract B3-style trace ID from \"";
      append(prefix, *found);
      prefix += "\": ";
      return error->with_prefix(prefix);
    }
    result.trace_id = *parsed;
  }

  auto parent_id =
      extract_id_header(headers, "x-b3-spanid", "parent span", "B3", 16);
  if (auto* error = parent_id.if_error()) {
    return std::move(*error);
  }
  result.parent_id = *parent_id;

  const StringView sampling_priority_header = "x-b3-sampled";
  if (auto found = headers.lookup(sampling_priority_header)) {
    auto sampling_priority = parse_int(*found, 10);
    if (auto* error = sampling_priority.if_error()) {
      std::string prefix;
      prefix += "Could not extract B3-style sampling priority from ";
      append(prefix, sampling_priority_header);
      prefix += ": ";
      append(prefix, *found);
      prefix += ' ';
      return error->with_prefix(prefix);
    }
    result.sampling_priority = *sampling_priority;
  }

  return result;
}

Expected<ExtractedData> extract_none(
    const DictReader&, std::unordered_map<std::string, std::string>&, Logger&) {
  ExtractedData result;
  result.style = PropagationStyle::NONE;
  return result;
}

std::string extraction_error_prefix(
    const Optional<PropagationStyle>& style,
    const std::vector<std::pair<std::string, std::string>>& headers_examined) {
  std::ostringstream stream;
  stream << "While extracting trace context";
  if (style) {
    stream << " in the " << to_json(*style) << " propagation style";
  }
  auto it = headers_examined.begin();
  if (it != headers_examined.end()) {
    stream << " from the following headers: [";
    stream << nlohmann::json(it->first + ": " + it->second);
    for (++it; it != headers_examined.end(); ++it) {
      stream << ", ";
      stream << nlohmann::json(it->first + ": " + it->second);
    }
    stream << "]";
  }
  stream << ", an error occurred: ";
  return stream.str();
}

AuditedReader::AuditedReader(const DictReader& underlying)
    : underlying(underlying) {}

Optional<StringView> AuditedReader::lookup(StringView key) const {
  auto value = underlying.lookup(key);
  if (value) {
    entries_found.emplace_back(key, *value);
  }
  return value;
}

void AuditedReader::visit(
    const std::function<void(StringView key, StringView value)>& visitor)
    const {
  underlying.visit([&, this](StringView key, StringView value) {
    entries_found.emplace_back(key, value);
    visitor(key, value);
  });
}

}  // namespace tracing
}  // namespace datadog