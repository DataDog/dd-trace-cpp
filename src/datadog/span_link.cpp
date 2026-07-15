#include "span_link.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <functional>
#include <string>
#include <tuple>

#include "msgpack.h"

namespace datadog::tracing {

Expected<void> msgpack_encode(std::string& destination, const SpanLink& link) {
  // array of (is_present, field_name, pack_function)
  const std::array<
      std::tuple<bool, StringView, std::function<Expected<void>(std::string&)>>,
      6>
      fields{{
          {true, "trace_id",
           [&](std::string& destination) {
             msgpack::pack_integer(destination, link.trace_id.low);
             return Expected<void>{};
           }},
          {true, "span_id",
           [&](std::string& destination) {
             msgpack::pack_integer(destination, link.span_id);
             return Expected<void>{};
           }},
          {true, "trace_id_high",
           [&](std::string& destination) {
             msgpack::pack_integer(destination, link.trace_id.high);
             return Expected<void>{};
           }},
          {!link.attributes.empty(), "attributes",
           [&](std::string& destination) {
             return msgpack::pack_map(
                 destination, link.attributes,
                 [](std::string& destination, const auto& value) {
                   return msgpack::pack_string(destination, value);
                 });
           }},
          {link.tracestate.has_value() && !link.tracestate->empty(),
           "tracestate",
           [&](std::string& destination) {
             return msgpack::pack_string(destination, *link.tracestate);
           }},
          {link.flags.has_value(), "flags",
           [&](std::string& destination) {
             // The high bit marks "flags is present" so a receiver can
             // distinguish an explicit value of 0 from an omitted field.
             msgpack::pack_integer(destination,
                                   std::uint64_t(*link.flags | (1u << 31)));
             return Expected<void>{};
           }},
      }};

  const auto size = std::count_if(fields.begin(), fields.end(),
                                  [](const auto& f) { return std::get<0>(f); });

  auto result = msgpack::pack_map(destination, size);
  if (!result) return result;

  for (const auto& [present, field, pack] : fields) {
    if (present) {
      result = msgpack::pack_map_suffix(destination, field, pack);
      if (!result) break;
    }
  }
  return result;
}

}  // namespace datadog::tracing
