#pragma once

// This component provides facilities for generating sequences of IDs used as
// span IDs and trace IDs.
//
// `IDGenerator` is an alias for `std::function<std::uint64_t()>`.
//
// `default_id_generator` is an `IDGenerator` that produces a thread-local
// pseudo-random sequence of uniformly distributed 63-bit unsigned integers. The
// sequence is randomly seeded one per thread and anytime the process forks.
// The IDs are 63-bit (instead of 64-bit) to ease compatibility with peer
// runtimes that don't have a native 64-bit unsigned numeric type.

#include <cstdint>
#include <functional>

namespace datadog {
namespace tracing {

using IDGenerator = std::function<std::uint64_t()>;

extern const IDGenerator default_id_generator;

}  // namespace tracing
}  // namespace datadog
