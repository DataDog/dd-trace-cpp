#pragma once

// TODO: document

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

}  // namespace tracing
}  // namespace datadog
