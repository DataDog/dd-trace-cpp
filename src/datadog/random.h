#pragma once

// This component provides a function, `random_uint64`, that generates
// pseudo-random numbers.

#include <cstdint>

namespace datadog {
namespace tracing {

// Return a pseudo-random unsigned 64-bit integer. The sequence generated is
// thread-local and seeded randomly. The thread-local generator is reseeded when
// this process forks.
std::uint64_t random_uint64();

}  // namespace tracing
}  // namespace datadog
