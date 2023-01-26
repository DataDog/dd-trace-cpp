#include "id_generator.h"

#include <bitset>

#include "random.h"

namespace datadog {
namespace tracing {
namespace {

class DefaultIDGenerator : public IDGenerator {
  const bool trace_id_128_bit_;

 public:
  explicit DefaultIDGenerator(bool trace_id_128_bit)
      : trace_id_128_bit_(trace_id_128_bit) {}

  TraceID trace_id() const override {
    TraceID result;
    result.low = random_uint64();
    if (trace_id_128_bit_) {
      result.high = random_uint64();
    } else {
      // In 64-bit mode, zero the most significant bit for compatibility with
      // older tracers that can't accept values above
      // `numeric_limits<int64_t>::max()`.
      std::bitset<64> bits = result.low;
      bits[63] = 0;
      result.low = bits.to_ullong();
    }
    return result;
  }

  std::uint64_t span_id() const override {
    // Zero the most significant bit for compatibility with older tracers that
    // can't accept values above `numeric_limits<int64_t>::max()`.
    std::bitset<64> bits = random_uint64();
    bits[63] = 0;
    return bits.to_ullong();
  }
};

}  // namespace

std::shared_ptr<const IDGenerator> default_id_generator(bool trace_id_128_bit) {
  return std::make_shared<DefaultIDGenerator>(trace_id_128_bit);
}

}  // namespace tracing
}  // namespace datadog
