#include "span_data.h"

#include <string_view>

#include "msgpackpp.h"

namespace datadog {
namespace tracing {

std::optional<Error> msgpack_encode(std::string& destination,
                                    const SpanData& span) try {
  msgpackpp::packer packer(&destination);

  // Be sure to update `num_fields` when adding fields.
  const int num_fields = 12;
  packer.pack_map(num_fields);

  packer.pack_str("service");
  packer.pack_str(span.service);

  packer.pack_str("name");
  packer.pack_str(span.name);

  packer.pack_str("resource");
  packer.pack_str(span.resource);

  packer.pack_str("trace_id");
  packer.pack_integer(span.trace_id);

  packer.pack_str("span_id");
  packer.pack_integer(span.span_id);

  packer.pack_str("parent_id");
  packer.pack_integer(span.parent_id);

  packer.pack_str("start");
  packer.pack_integer(std::chrono::duration_cast<std::chrono::nanoseconds>(
                          span.start.wall.time_since_epoch())
                          .count());

  packer.pack_str("duration");
  packer.pack_integer(
      std::chrono::duration_cast<std::chrono::nanoseconds>(span.duration)
          .count());

  packer.pack_str("error");
  packer.pack_integer(std::int32_t(span.error));

  packer.pack_str("meta");
  packer.pack_map(span.tags.size());
  for (const auto& [key, value] : span.tags) {
    packer.pack_str(key);
    packer.pack_str(value);
  }

  packer.pack_str("metrics");
  packer.pack_map(span.numeric_tags.size());
  for (const auto& [key, value] : span.numeric_tags) {
    packer.pack_str(key);
    packer.pack_double(value);
  }

  packer.pack_str("type");
  packer.pack_str(span.type);

  // Be sure to update `num_fields` when adding fields.
  return std::nullopt;
} catch (const msgpackpp::pack_error& error) {
  return Error{Error::MESSAGEPACK_ENCODE_FAILURE, error.what()};
}  // TODO: Should we worry about std::bad_alloc too?

}  // namespace tracing
}  // namespace datadog