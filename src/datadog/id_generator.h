#pragma once

#include <cstdint>
#include <functional>

namespace datadog {
namespace tracing {

using IDGenerator = std::function<std::uint64_t()>;

extern const IDGenerator default_id_generator;

}  // namespace tracing
}  // namespace datadog
