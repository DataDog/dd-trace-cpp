#pragma once

#include <cstdint>
#include <functional>

namespace datadog {
namespace tracing {

struct IDGenerator {
  std::function<std::uint64_t()> generate_trace_id;
  std::function<std::uint64_t()> generate_span_id;
};

extern const IDGenerator default_id_generator;

}  // namespace tracing
}  // namespace datadog
