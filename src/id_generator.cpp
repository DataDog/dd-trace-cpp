#include "id_generator.h"

#include <cstddef>
#include <limits>
#include <random>

namespace datadog {
namespace tracing {
namespace {

// TODO: no
template <typename Integer>
Integer random() {
  std::random_device randomness;
  std::uniform_int_distribution<Integer> distribution{
      std::numeric_limits<Integer>::min(), std::numeric_limits<Integer>::max()};
  return distribution(randomness);
}
// end TODO

}  // namespace

const IDGenerator default_id_generator = {
    // TODO: no
    &random<std::uint64_t>,
    // TODO: no
    &random<std::uint64_t>};

}  // namespace tracing
}  // namespace datadog
