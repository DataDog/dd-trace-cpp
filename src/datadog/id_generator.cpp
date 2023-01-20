#include "id_generator.h"

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
    }
    return result;
  }

  std::uint64_t span_id() const override { return random_uint64(); }
};

}  // namespace

std::shared_ptr<const IDGenerator> default_id_generator(bool trace_id_128_bit) {
  return std::make_shared<DefaultIDGenerator>(trace_id_128_bit);
}

}  // namespace tracing
}  // namespace datadog
