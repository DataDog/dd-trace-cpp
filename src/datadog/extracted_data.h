#pragma once

// This component provides a `struct`, `ExtractedData`, that stores fields
// extracted from trace context. It's an implementation detail of this library.

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
  // If this `ExtractedData` was created on account of `PropagationStyle::W3C`
  // and trace context was successfully extracted, then `full_w3c_trace_id_hex`
  // contains the hex-encoded 128-bit trace ID. `trace_id` will be the least
  // significant 64 bits of the same value. `full_w3c_trace_id_hex` is used for
  // the `W3C` injection style.
  Optional<std::string> full_w3c_trace_id_hex;
  // If this `ExtractedData` was created on account of `PropagationStyle::W3C`,
  // then `additional_w3c_tracestate` contains the parts of the "tracestate"
  // header that are not the "dd" (Datadog) entry. If there are no other parts,
  // then `additional_w3c_tracestate` is null.
  // `additional_w3c_tracestate` is used for the `W3C` injection style.
  Optional<std::string> additional_w3c_tracestate;
  // If this `ExtractedData` was created on account of `PropagationStyle::W3C`,
  // and if the "tracestate" header contained a "dd" (Datadog) entry, then
  // `additional_datadog_w3c_tracestate` contains fields from within the "dd"
  // entry that were not interpreted. If there are no such fields, then
  // `additional_datadog_w3c_tracestate` is null.
  // `additional_datadog_w3c_tracestate` is used for the `W3C` injection style.
  Optional<std::string> additional_datadog_w3c_tracestate;
};

}  // namespace tracing
}  // namespace datadog