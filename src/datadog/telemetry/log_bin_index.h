#pragma once

// Bin index of the log-binned distributions. See log_binned_distr.h for the
// mapping.

#include <cstdint>
#include <limits>

namespace datadog::telemetry::log_bins {

// The number of a bin. Same type as LogBinnedDistr's private BinIndex.
using BinIndex = std::int32_t;

// Each bin's upper edge is this many times its lower edge.
inline constexpr double bin_ratio = 65.0 / 64.0;
// Shift so that 1e-9 falls in bin 1.
inline constexpr double bin_index_shift = 1338.5;
// Values below this, zero included, are counted apart.
inline constexpr double zero_threshold =
    std::numeric_limits<double>::min() * bin_ratio;

// Special results of bin_index().
inline constexpr BinIndex zero_bin = std::numeric_limits<BinIndex>::min();
inline constexpr BinIndex rejected_value = zero_bin + 1;

// Returns the bin of `value`. Returns `zero_bin` for values below
// `zero_threshold`, and `rejected_value` for negative values, NaN and
// infinities. Same result as reference_bin_index(), but faster.
BinIndex bin_index(double value);

// Computes the bin as floor(ln(value) × m + bin_index_shift), with
// m = 1 / ln(bin_ratio). The multiplication and the addition are rounded once.
// Declared here for the tests. The library only uses bin_index().
BinIndex reference_bin_index(double value);

// Returns true if the fast table was built. Declared here for the tests.
bool fast_table_built();

}  // namespace datadog::telemetry::log_bins
