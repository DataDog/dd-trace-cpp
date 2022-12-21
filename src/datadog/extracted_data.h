#pragma once

// TODO: document

#include <cstdint>
#include <string>
#include <unordered_map>

#include "optional.h"

namespace datadog {
namespace tracing {

struct ExtractedData {
  Optional<std::uint64_t> trace_id;
  Optional<std::uint64_t> parent_id;
  Optional<std::string> origin;
  std::unordered_map<std::string, std::string> trace_tags;
  Optional<int> sampling_priority;
  // TODO: document
  Optional<std::string> full_w3c_trace_id_hex;
  // TODO: document
  Optional<std::string> additional_w3c_tracestate;
  // TODO: document
  Optional<std::string> additional_datadog_w3c_tracestate;
};

}  // namespace tracing
}  // namespace datadog
