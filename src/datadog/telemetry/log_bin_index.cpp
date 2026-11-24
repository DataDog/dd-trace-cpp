#include "log_bin_index.h"

#include <cmath>

namespace datadog::telemetry::log_bins {
namespace {

double log_multiplier();

}  // namespace

BinIndex bin_index(double value) {
  // Checks the bits, because -ffast-math can remove std::isnan() and friends.
  const std::uint64_t bits = to_bits(value);
  const bool not_finite = ((bits >> 52) & 0x7ff) == 0x7ff;
  const bool below_zero = (bits >> 63) != 0 && (bits << 1) != 0;
  if (not_finite || below_zero) return rejected_value;
  if (value < zero_threshold) return zero_bin;

  // One rounding for the multiplication and the addition. Separate roundings
  // would depend on whether the compiler fuses them.
  const double scaled_log =
      std::fma(std::log(value), log_multiplier(), bin_index_shift);
  return static_cast<BinIndex>(std::floor(scaled_log));
}

namespace {

// Same formula as libdatadog: 1 / ln(bin_ratio).
double log_multiplier() {
  static const double multiplier = 1.0 / std::log(bin_ratio);
  return multiplier;
}

}  // namespace
}  // namespace datadog::telemetry::log_bins
