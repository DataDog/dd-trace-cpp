#include <datadog/span_link.h>

#include <cstddef>
#include <string>

#include "msgpack.h"

namespace datadog {
namespace tracing {


Expected<void> msgpack_encode(std::string& destination, const SpanLink& link) {
  const bool has_trace_id_high = link.trace_id.high != 0;
  const bool has_attributes = !link.attributes.empty();
  const bool has_tracestate = link.tracestate && !link.tracestate->empty();
  const bool has_flags = link.flags.has_value();

  std::size_t size = 2;  // trace_id + span_id are always present
  if (has_trace_id_high) ++size;
  if (has_attributes) ++size;
  if (has_tracestate) ++size;
  if (has_flags) ++size;

  auto result = msgpack::pack_map(destination, size);
  if (!result) return result;

  // trace_id (low 64 bits)
  result = msgpack::pack_string(destination, "trace_id");
  if (!result) return result;
  msgpack::pack_integer(destination, link.trace_id.low);

  if (has_trace_id_high) {
    result = msgpack::pack_string(destination, "trace_id_high");
    if (!result) return result;
    msgpack::pack_integer(destination, link.trace_id.high);
  }

  result = msgpack::pack_string(destination, "span_id");
  if (!result) return result;
  msgpack::pack_integer(destination, link.span_id);

  if (has_attributes) {
    result = msgpack::pack_string(destination, "attributes");
    if (!result) return result;
    result = msgpack::pack_map(
        destination, link.attributes,
        [](std::string& destination, const auto& value) {
          return msgpack::pack_string(destination, value);
        });
    if (!result) return result;
  }

  if (has_tracestate) {
    result = msgpack::pack_string(destination, "tracestate");
    if (!result) return result;
    result = msgpack::pack_string(destination, *link.tracestate);
    if (!result) return result;
  }

  if (has_flags) {
    result = msgpack::pack_string(destination, "flags");
    if (!result) return result;
    // The high bit marks "flags is present" so a receiver can distinguish an
    // explicit value of 0 from an omitted field.
    msgpack::pack_integer(destination,
                          std::uint64_t(*link.flags | (1u << 31)));
  }

  return nullopt;
}

}  // namespace tracing
}  // namespace datadog
