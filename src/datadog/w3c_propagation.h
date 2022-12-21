#pragma once

// TODO: document

#include <cstdint>
#include <string>
#include <unordered_map>

#include "expected.h"
#include "extracted_data.h"
#include "optional.h"

namespace datadog {
namespace tracing {

class DictReader;

// Return `ExtractedData` deduced from the "traceparent" and "tracestate"
// entries of the specified `headers`. If an error occurs, set a value for the
// `tags::internal::w3c_extraction_error` tag in the specified `span_tags`.
// `extract_w3c` will not return an error; instead, it returns an empty
// `ExtractedData` when extraction fails.
Expected<ExtractedData> extract_w3c(
    const DictReader& headers,
    std::unordered_map<std::string, std::string>& span_tags);

// Return a value for the "traceparent" header consisting of the specified
// `trace_id` or the optionally specified `full_w3c_trace_id_hex` as the trace
// ID, the specified `span_id` as the parent ID, and trace flags deduced from
// the specified `sampling_priority`.
std::string encode_traceparent(
    std::uint64_t trace_id, const Optional<std::string>& full_w3c_trace_id_hex,
    std::uint64_t span_id, int sampling_priority);

}  // namespace tracing
}  // namespace datadog
