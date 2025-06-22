#include <datadog/trace_source.h>

#include "parse_util.h"

namespace datadog {
namespace tracing {

bool validate_trace_source(StringView source_str) {
  if (source_str.size() > 2) return false;

  auto maybe_ts_uint = parse_uint64(source_str, 10);
  if (maybe_ts_uint.if_error()) return false;

  // Bit twiddling magic is coming from
  // <http://www.graphics.stanford.edu/~seander/bithacks.html> <3.
  auto is_power_of_2 = [](uint64_t v) -> bool { return v && !(v & (v - 1)); };

  return is_power_of_2(*maybe_ts_uint);
}

}  // namespace tracing
}  // namespace datadog
