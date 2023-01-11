#include "trace_id.h"

#include "hex.h"
#include "parse_util.h"

namespace datadog {
namespace tracing {

TraceID::TraceID() : low(0) {}

TraceID::TraceID(std::uint64_t low) : low(low) {}

TraceID::TraceID(std::uint64_t low, std::uint64_t high)
    : low(low), high(high) {}

std::string TraceID::hex() const {
  std::string result;
  if (high && *high) {
    result += ::datadog::tracing::hex(*high);
  }
  result += ::datadog::tracing::hex(low);
  return result;
}

std::string TraceID::debug() const {
  if (high) {
    return "0x" + hex();
  }
  return std::to_string(low);
}

Expected<TraceID> TraceID::parse_hex(StringView input) {
  const auto parse_hex_piece =
      [&](StringView piece) -> Expected<std::uint64_t> {
    auto result = parse_uint64(piece, 16);
    if (auto *error = result.if_error()) {
      std::string prefix = "Unable to parse trace ID from \"";
      append(prefix, input);
      prefix += "\": ";
      return error->with_prefix(prefix);
    }
    return result;
  };

  // A 64-bit integer is at most 16 hex characters.  If the input is no
  // longer than that, then it will all fit in `TraceID::low`.
  if (input.size() <= 16) {
    auto result = parse_hex_piece(input);
    if (auto *error = result.if_error()) {
      return std::move(*error);
    }
    return TraceID(*result);
  }

  // Parse the lower part and the higher part separately.
  const auto divider = input.begin() + (input.size() - 16);
  const auto high_hex = range(input.begin(), divider);
  const auto low_hex = range(divider, input.end());
  TraceID trace_id;

  auto result = parse_hex_piece(low_hex);
  if (auto *error = result.if_error()) {
    return std::move(*error);
  }
  trace_id.low = *result;

  result = parse_hex_piece(high_hex);
  if (auto *error = result.if_error()) {
    return std::move(*error);
  }
  trace_id.high = *result;

  return trace_id;
}

bool operator==(TraceID left, TraceID right) {
  return left.low == right.low && left.high == right.high;
}

bool operator!=(TraceID left, TraceID right) {
  return left.low != right.low || left.high != right.high;
}

bool operator==(TraceID left, std::uint64_t right) {
  if (left.high) {
    return *left.high == 0 && left.low == right;
  }
  return left.low == right;
}

bool operator!=(TraceID left, std::uint64_t right) {
  if (left.high) {
    return *left.high != 0 || left.low != right;
  }
  return left.low != right;
}

}  // namespace tracing
}  // namespace datadog
