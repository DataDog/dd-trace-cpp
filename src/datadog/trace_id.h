#pragma once

// TODO: document

#include <cstdint>
#include <string>

#include "expected.h"
#include "string_view.h"

namespace datadog {
namespace tracing {

struct TraceID {
  std::uint64_t low;
  std::uint64_t high;

  // TODO: document
  TraceID();
  explicit TraceID(std::uint64_t low);
  TraceID(std::uint64_t low, std::uint64_t high);

  // TODO: document
  std::string hex() const;
  std::string debug() const;

  // TODO: document
  static Expected<TraceID> parse_hex(StringView);
};

bool operator==(TraceID, TraceID);
bool operator!=(TraceID, TraceID);
bool operator==(TraceID, std::uint64_t);
bool operator!=(TraceID, std::uint64_t);

}  // namespace tracing
}  // namespace datadog
