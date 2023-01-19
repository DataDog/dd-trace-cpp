#include "w3c_propagation.h"

#include <algorithm>
#include <cassert>
#include <cstddef>
#include <regex>
#include <utility>

#include "dict_reader.h"
#include "hex.h"
#include "parse_util.h"
#include "tags.h"

namespace datadog {
namespace tracing {
namespace {

// Return a predicate that returns whether its `char` argument is any of the
// following:
//
// - outside of the ASCII inclusive range `[lowest_ascii, highest_ascii]`
// - equal to one of the `disallowed_characters`.
//
// `verboten` is used with `std::replace_if` to sanitize field values within the
// tracestate header.
auto verboten(int lowest_ascii, int highest_ascii,
              StringView disallowed_characters) {
  return [=, chars = disallowed_characters](char ch) {
    return int(ch) < lowest_ascii || int(ch) > highest_ascii ||
           std::find(chars.begin(), chars.end(), ch) != chars.end();
  };
}

// Populate the specified `result` with data extracted from the "traceparent"
// entry of the specified `headers`. Return `nullopt` on success. Return a value
// for the `tags::internal::w3c_extraction_error` tag if an error occurs.
Optional<std::string> extract_traceparent(ExtractedData& result,
                                          const DictReader& headers) {
  const auto maybe_traceparent = headers.lookup("traceparent");
  if (!maybe_traceparent) {
    return nullopt;
  }

  const auto traceparent = strip(*maybe_traceparent);

  // Note that leading and trailing whitespace was already removed above.
  // Note that match group 0 is the entire match.
  static const auto& pattern =
      "([0-9a-f]{2})"  // hex version number (match group 1)
      "-"
      "([0-9a-f]{32})"  // hex trace ID (match group 2)
      "-"
      "([0-9a-f]{16})"  // hex parent span ID (match group 3)
      "-"
      "([0-9a-f]{2})"  // hex "trace-flags" (match group 4)
      "(?:$|-.*)";     // either the end, or a hyphen preceding further fields
  static const std::regex regex{pattern};

  std::match_results<StringView::iterator> match;
  if (!std::regex_match(traceparent.begin(), traceparent.end(), match, regex)) {
    return "malformed_traceparent";
  }

  assert(match.ready());
  assert(match.size() == 4 + 1);

  const auto to_string_view = [](const auto& submatch) {
    assert(submatch.first <= submatch.second);
    return StringView{submatch.first,
                      std::size_t(submatch.second - submatch.first)};
  };

  if (to_string_view(match[1]) == "ff") {
    return "invalid_version";
  }

  result.trace_id = *TraceID::parse_hex(to_string_view(match[2]));
  if (result.trace_id == 0) {
    return "trace_id_zero";
  }

  result.parent_id = *parse_uint64(to_string_view(match[3]), 16);
  if (*result.parent_id == 0) {
    return "parent_id_zero";
  }

  const auto flags = *parse_uint64(to_string_view(match[4]), 16);
  result.sampling_priority = int(flags & 1);

  return nullopt;
}

// `struct PartiallyParsedTracestat` contains the separated Datadog-specific and
// non-Datadog-specific portions of tracestate.
struct PartiallyParsedTracestate {
  StringView datadog_value;
  std::string other_entries;
};

// Return the separate Datadog-specific and non-Datadog-specific portions of the
// specified `tracestate`. If `tracestate` does not have a Datadog-specific
// portion, return `nullopt`.
Optional<PartiallyParsedTracestate> parse_tracestate(StringView tracestate) {
  Optional<PartiallyParsedTracestate> result;

  const char* const begin = tracestate.begin();
  const char* const end = tracestate.end();
  const char* pair_begin = begin;
  while (pair_begin != end) {
    const char* const pair_end = std::find(pair_begin, end, ',');
    // Note that since this `pair` is `strip`ped, `pair_begin` is not
    // necessarily equal to `pair.begin()` (similarly for the ends).
    const auto pair = strip(range(pair_begin, pair_end));
    if (pair.empty()) {
      pair_begin = pair_end == end ? end : pair_end + 1;
      continue;
    }

    const auto kv_separator = std::find(pair.begin(), pair.end(), '=');
    if (kv_separator == pair.end()) {
      // This is an invalid entry because it contains a non-whitespace character
      // but not a "=".
      // Let's move on to the next entry.
      pair_begin = pair_end == end ? end : pair_end + 1;
      continue;
    }

    const auto key = range(pair.begin(), kv_separator);
    if (key != "dd") {
      // On to the next.
      pair_begin = pair_end == end ? end : pair_end + 1;
      continue;
    }

    // We found the "dd" entry.
    result.emplace();
    result->datadog_value = range(kv_separator + 1, pair.end());
    // `result->other_entries` is whatever was before the "dd" entry and
    // whatever is after the "dd" entry, but without an extra comma in the
    // middle.
    if (pair_begin != begin) {
      // There's a prefix
      append(result->other_entries, range(begin, pair_begin - 1));
      if (pair_end != end) {
        // and a suffix
        append(result->other_entries, range(pair_end, end));
      }
    } else if (pair_end != end) {
      // There's just a suffix
      append(result->other_entries, range(pair_end + 1, end));
    }

    break;
  }

  return result;
}

// Fill the specified `result` with information parsed from the specified
// `datadog_value`. `datadog_value` is the value of the "dd" entry in the
// "tracestate" header.
//
// `parse_datadog_tracestate` populates the following `ExtractedData` fields:
//
// - `origin`
// - `trace_tags`
// - `sampling_priority`
// - `additional_datadog_w3c_tracestate`
void parse_datadog_tracestate(ExtractedData& result, StringView datadog_value) {
  const char* const begin = datadog_value.begin();
  const char* const end = datadog_value.end();
  const char* pair_begin = begin;
  while (pair_begin != end) {
    const char* const pair_end = std::find(pair_begin, end, ';');
    const auto pair = range(pair_begin, pair_end);
    if (pair.empty()) {
      // chaff!
      pair_begin = pair_end == end ? end : pair_end + 1;
      continue;
    }

    const auto kv_separator = std::find(pair_begin, pair_end, ':');
    if (kv_separator == pair_end) {
      // chaff!
      pair_begin = pair_end == end ? end : pair_end + 1;
      continue;
    }

    const auto key = range(pair_begin, kv_separator);
    const auto value = range(kv_separator + 1, pair_end);
    if (key == "o") {
      result.origin = std::string{value};
    } else if (key == "s") {
      const auto maybe_priority = parse_int(value, 10);
      if (!maybe_priority) {
        // chaff!
        pair_begin = pair_end == end ? end : pair_end + 1;
        continue;
      }
      const int priority = *maybe_priority;
      // If we didn't parse a sampling priority from traceparent, or if the one
      // we just parsed from tracestate is consistent with the previous, then
      // set the sampling priority to the one we just parsed.
      // Alternatively, if we already parsed a sampling priority from
      // traceparent and got a result inconsistent with that parsed here, go
      // with the one previously parsed from traceparent.
      if (!result.sampling_priority ||
          (*result.sampling_priority > 0) == (priority > 0)) {
        result.sampling_priority = priority;
      }
    } else if (starts_with(key, "t.")) {
      // The part of the key that follows "t." is the name of a trace tag,
      // except without the "_dd.p." prefix.
      const auto tag_suffix = key.substr(2);
      std::string tag_name = "_dd.p.";
      append(tag_name, tag_suffix);
      // The "_dd.p.tid" trace tag is ignored in this context, since its value
      // is better inferred from the higher 64 bits of the trace ID.
      if (tag_name == "_dd.p.tid") {
        pair_begin = pair_end == end ? end : pair_end + 1;
        continue;
      }
      // The tag value was encoded with all '=' replaced by '~'.  Undo that
      // transformation.
      std::string decoded_value{value};
      std::replace(decoded_value.begin(), decoded_value.end(), '~', '=');
      result.trace_tags.emplace_back(std::move(tag_name),
                                     std::move(decoded_value));
    } else {
      // Unrecognized key: append the whole pair to
      // `additional_datadog_w3c_tracestate`, which will be used if/when we
      // inject trace context.
      auto& entries = result.additional_datadog_w3c_tracestate;
      if (!entries) {
        entries.emplace();
      } else {
        *entries += ';';
      }
      append(*entries, pair);
    }

    pair_begin = pair_end == end ? end : pair_end + 1;
  }
}

// Fill the specified `result` with information parsed from the "tracestate"
// element of the specified `headers`, if present.
//
// `extract_tracestate` populates the `additional_w3c_tracestate` field of
// `ExtractedData`, in addition to those populated by
// `parse_datadog_tracestate`.
void extract_tracestate(ExtractedData& result, const DictReader& headers) {
  const auto maybe_tracestate = headers.lookup("tracestate");
  if (!maybe_tracestate) {
    return;
  }

  const auto tracestate = strip(*maybe_tracestate);
  auto maybe_parsed = parse_tracestate(tracestate);
  if (!maybe_parsed) {
    // No "dd" entry in `tracestate`, so there's nothing to extract.
    if (!tracestate.empty()) {
      result.additional_w3c_tracestate = std::string{tracestate};
    }
    return;
  }

  auto& [datadog_value, other_entries] = *maybe_parsed;
  if (!other_entries.empty()) {
    result.additional_w3c_tracestate = std::move(other_entries);
  }

  parse_datadog_tracestate(result, datadog_value);
}

}  // namespace

Expected<ExtractedData> extract_w3c(
    const DictReader& headers,
    std::unordered_map<std::string, std::string>& span_tags, Logger&) {
  ExtractedData result;

  if (auto error_tag_value = extract_traceparent(result, headers)) {
    span_tags[tags::internal::w3c_extraction_error] =
        std::move(*error_tag_value);
    return ExtractedData{};
  }

  // If we didn't get a trace ID from traceparent, don't bother with
  // tracestate.
  if (!result.trace_id) {
    return result;
  }

  extract_tracestate(result, headers);

  return result;
}

std::string encode_traceparent(TraceID trace_id, std::uint64_t span_id,
                               int sampling_priority) {
  std::string result;
  // version
  result += "00-";

  // trace ID
  result += trace_id.hex_padded();
  result += '-';

  // span ID
  result += hex_padded(span_id);
  result += '-';

  // flags
  result += sampling_priority > 0 ? "01" : "00";

  return result;
}

std::string encode_datadog_tracestate(
    int sampling_priority, const Optional<std::string>& origin,
    const std::vector<std::pair<std::string, std::string>>& trace_tags,
    const Optional<std::string>& additional_datadog_w3c_tracestate) {
  std::string result = "dd=s:";
  result += std::to_string(sampling_priority);

  if (origin) {
    result += ";o:";
    result += *origin;
    std::replace_if(result.end() - origin->size(), result.end(),
                    verboten(0x20, 0x7e, ",;="), '_');
  }

  for (const auto& [key, value] : trace_tags) {
    const StringView prefix = "_dd.p.";
    if (!starts_with(key, prefix) || key == tags::internal::trace_id_high) {
      // Either it's not a propagation tag, or it's one of the propagation tags
      // that need not be included in tracestate.
      continue;
    }

    // `key` is "_dd.p.<name>", but we want "t.<name>".
    result += ";t.";
    result.append(key, prefix.size());
    std::replace_if(result.end() - (key.size() - prefix.size()), result.end(),
                    verboten(0x20, 0x7e, " ,;="), '_');

    result += ':';
    result += value;
    std::replace_if(result.end() - value.size(), result.end(),
                    verboten(0x20, 0x7e, ",;~"), '_');
    // `value` might contain equal signs ("="), which is reserved in tracestate.
    // Replace them with tildes ("~").
    std::replace(result.end() - value.size(), result.end(), '=', '~');
  }

  if (additional_datadog_w3c_tracestate) {
    result += ';';
    result += *additional_datadog_w3c_tracestate;
  }

  const std::size_t max_size = 256;
  while (result.size() > max_size) {
    const auto last_semicolon_index = result.rfind(';');
    // This assumption is safe, because `result` always begins with
    // "dd=s:<int>", and that's fewer than `max_size` characters for any
    // `<int>`.
    assert(last_semicolon_index != std::string::npos);
    result.resize(last_semicolon_index);
  }

  return result;
}

std::string encode_tracestate(
    int sampling_priority, const Optional<std::string>& origin,
    const std::vector<std::pair<std::string, std::string>>& trace_tags,
    const Optional<std::string>& additional_datadog_w3c_tracestate,
    const Optional<std::string>& additional_w3c_tracestate) {
  std::string result = encode_datadog_tracestate(
      sampling_priority, origin, trace_tags, additional_datadog_w3c_tracestate);

  if (additional_w3c_tracestate) {
    result += ',';
    result += *additional_w3c_tracestate;
  }

  return result;
}

}  // namespace tracing
}  // namespace datadog
