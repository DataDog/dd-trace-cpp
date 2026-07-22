#include "span_link.h"

#include <cstddef>
#include <string>

#include "msgpack.h"

namespace datadog::tracing {

Expected<void> msgpack_encode(std::string& destination, const SpanLink& link) {
  const bool has_attributes = !link.attributes.empty();
  const bool has_tracestate =
      link.context.tracestate.has_value() && !link.context.tracestate->empty();
  const bool has_flags = link.context.flags.has_value();

  std::size_t size = 3;  // trace_id, span_id, and trace_id_high are always
                         // present.
  if (has_attributes) ++size;
  if (has_tracestate) ++size;
  if (has_flags) ++size;

  Expected<void> result = msgpack::pack_map(destination, size);
  if (!result) return result;

  result = msgpack::pack_map_suffix(
      destination, "trace_id",
      [&](std::string& destination) {
        msgpack::pack_integer(destination, link.context.trace_id.low);
        return Expected<void>{};
      },
      "span_id",
      [&](std::string& destination) {
        msgpack::pack_integer(destination, link.context.span_id);
        return Expected<void>{};
      },
      "trace_id_high",
      [&](std::string& destination) {
        msgpack::pack_integer(destination, link.context.trace_id.high);
        return Expected<void>{};
      });
  if (!result) return result;

  if (has_attributes) {
    result = msgpack::pack_map_suffix(
        destination, "attributes", [&](std::string& destination) {
          return msgpack::pack_map(
              destination, link.attributes,
              [](std::string& destination, const std::string& value) {
                return msgpack::pack_string(destination, value);
              });
        });
    if (!result) return result;
  }

  if (has_tracestate) {
    result = msgpack::pack_map_suffix(
        destination, "tracestate", [&](std::string& destination) {
          return msgpack::pack_string(destination, *link.context.tracestate);
        });
    if (!result) return result;
  }

  if (has_flags) {
    result = msgpack::pack_map_suffix(
        destination, "flags", [&](std::string& destination) {
          // The high bit marks "flags is present" so a receiver can
          // distinguish an explicit value of 0 from an omitted field.
          msgpack::pack_integer(
              destination, std::uint64_t(*link.context.flags | (1u << 31)));
          return Expected<void>{};
        });
    if (!result) return result;
  }

  return result;
}

}  // namespace datadog::tracing
