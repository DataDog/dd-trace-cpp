#pragma once

// TODO

#include <cstdint>
#include <string>

#include "expected.h"
#include "optional.h"
#include "string_view.h"

namespace datadog {
namespace tracing {

struct TraceID {
  std::uint64_t low;
  Optional<std::uint64_t> high;

  TraceID();
  explicit TraceID(std::uint64_t low);
  TraceID(std::uint64_t low, std::uint64_t high);

  std::string hex() const;
  std::string low_dec() const;

  static Expected<TraceID> parse_hex(StringView);
};

bool operator==(TraceID, TraceID);
bool operator!=(TraceID, TraceID);
bool operator==(TraceID, std::uint64_t);
bool operator!=(TraceID, std::uint64_t);

}  // namespace tracing
}  // namespace datadog
