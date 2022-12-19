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

// TODO: document
Expected<ExtractedData> extract_w3c(
    const DictReader& headers,
    std::unordered_map<std::string, std::string>& span_tags);

}  // namespace tracing
}  // namespace datadog
