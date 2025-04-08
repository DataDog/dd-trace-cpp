#pragma once

#include <datadog/trace_id.h>

#include <array>
#include <cstdint>

// Global struct used to exposed thread-specific information.
// https://github.com/elastic/apm/blob/149cd3e39a77a58002344270ed2ad35357bdd02d/specs/agents/universal-profiling-integration.md#thread-local-storage-layout

namespace datadog {
namespace tracing {
struct __attribute__((packed)) TLSStorage {
  uint16_t layout_minor_version;
  uint8_t valid;
  uint8_t trace_present;
  uint8_t trace_flags;
  uint64_t trace_id_low;
  uint64_t trace_id_high;
  uint64_t span_id;
  uint64_t transaction_id;
};

}  // namespace tracing
}  // namespace datadog
