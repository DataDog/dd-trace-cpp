#include "w3c_propagation.h"

#include <cassert>
#include <cstddef>
#include <regex>
#include <utility>

#include "dict_reader.h"
#include "parse_util.h"
#include "tags.h"

namespace datadog {
namespace tracing {
namespace {

// TODO: document
Optional<std::string> extract_traceparent(ExtractedData& result,
                                          const DictReader& headers) {
  const auto maybe_traceparent = headers.lookup("traceparent");
  if (!maybe_traceparent) {
    return nullopt;
  }

  const auto traceparent = strip(*maybe_traceparent);

  // Note that leading and trailing whitespace was already removed above.
  // Note that the match group 0 is the entire match.
  static const auto& pattern =
      "([0-9a-f]{2})"  // hex version number (match group 1)
      "-"
      "([0-9a-f]{16}([0-9a-f]{16}))"  // hex trace ID (match groups 2 and 3)
      "-"
      "([0-9a-f]{16})"  // hex parent span ID (match group 4)
      "-"
      "([0-9a-f]{2})"  // hex "trace-flags" (match group 5)
      "(?:$|-.*)";     // either the end, or a hyphen preceding further fields
  static const std::regex regex{pattern};

  std::match_results<StringView::iterator> match;
  if (!std::regex_match(traceparent.begin(), traceparent.end(), match, regex)) {
    return "malformed_traceparent";
  }

  assert(match.ready());
  assert(match.size() == 5 + 1);

  const auto to_string_view = [](const auto& submatch) {
    assert(submatch.first <= submatch.second);
    return StringView{submatch.first,
                      std::size_t(submatch.second - submatch.first)};
  };

  if (to_string_view(match[1]) == "ff") {
    return "invalid_version";
  }

  result.full_w3c_trace_id_hex = std::string{to_string_view(match[2])};
  if (result.full_w3c_trace_id_hex->find_first_not_of('0') ==
      std::string::npos) {
    return "trace_id_zero";
  }

  result.trace_id = *parse_uint64(to_string_view(match[3]), 16);

  result.parent_id = *parse_uint64(to_string_view(match[4]), 16);
  if (*result.parent_id == 0) {
    return "parent_id_zero";
  }

  const auto flags = *parse_uint64(to_string_view(match[5]), 16);
  result.sampling_priority = int(flags & 1);

  return nullopt;
}

// TODO: document
Optional<std::string> extract_tracestate(ExtractedData& result,
                                         const DictReader& headers) {
  // TODO
  (void)result;
  (void)headers;
  return nullopt;
}

}  // namespace

Expected<ExtractedData> extract_w3c(
    const DictReader& headers,
    std::unordered_map<std::string, std::string>& span_tags) {
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

  if (auto error_tag_value = extract_tracestate(result, headers)) {
    span_tags[tags::internal::w3c_extraction_error] =
        std::move(*error_tag_value);
    // Carry on with whatever data was extracted from traceparent.
  }

  return result;
}

}  // namespace tracing
}  // namespace datadog
