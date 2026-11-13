#pragma once

#include <cstdint>
#include <limits>
#include <mutex>
#include <string>
#include <vector>

namespace datadog::telemetry {

// A distribution whose bins form a geometric series, compatible with
// DDSketch. Bin i covers [low(i), γ × low(i)), so each bin's upper edge is γ
// times its lower edge.
//
// Every value in bin i is reported as the same bin value x̂(i). It is the
// harmonic mean of the bin's edges:
//
//   x̂ = 2 × low × γ low / (low + γ low) = low × (1 + α)
//
// This puts x̂ at the same relative error from both edges. So for any value x
// in the bin, |x̂ - x| / x <= α.
//
// Design parameters:
// * The maximum relative error, α = 1/129. That is about 2.1 significant
//   digits. The bin ratio follows from it: γ = (1 + α) / (1 - α) = 65/64.
// * Where the bins start. Bin 1 is the bin that holds 1e-9, the smallest value
//   the backend's sketches use. So the bin number is
//
//     i(x) = floor(log_γ(x) + s),  with s = 1 - floor(log_γ(1e-9)) + 0.5
//
//   which gives s = 1338.5. Values between 0 and 1e-9 get bins below 1.
//
// Other properties:
// * Values below DBL_MIN × γ, zero included, are counted apart, in the zero
//   count. Negative values, NaN and infinities are rejected.
// * At most 2048 bins are kept: i_max - i_min <= 2047. When a new value would
//   go past that, the lowest bins are merged into the lowest bin kept. Values
//   below the range go to that lowest bin too. The highest bins are never
//   merged.
// * `log` rounds, so about one value in 10^12, right next to a bin edge, goes
//   to the neighbouring bin. The worst relative error is then
//   α + 4 × 10^-13. This also moves a few bin edges by a double or so between
//   C libraries, and from libdatadog's.
// * All member functions are thread-safe. Each distribution has its own lock.
class LogBinnedDistr {
 public:
  // Returns false if the value is rejected.
  bool record(double value);

  // Returns the distribution as a serialized DDSketch protobuf message. The
  // bins are in the sparse form (`binCounts`).
  std::string encode_protobuf() const;

 private:
  // The number of a bin, i(x) above. Same type as log_bins::BinIndex.
  using BinIndex = std::int32_t;

  void add_to_bin(BinIndex bin);
  void extend_range(BinIndex bin);
  void start_range(BinIndex bin);
  void extend_down(BinIndex bin, BinIndex highest);
  void extend_up(BinIndex bin, BinIndex highest);
  std::uint64_t take_counts(BinIndex lowest, BinIndex highest);
  void cover(BinIndex lowest, BinIndex highest);
  std::uint64_t& count_at(BinIndex bin);
  std::string encode_bins() const;

  mutable std::mutex mutex_;
  // counts_[k] is the count of bin window_start_ + k. Bins outside the range
  // [lowest_bin_, lowest_bin_ + bin_span_) are always zero.
  std::vector<std::uint64_t> counts_;
  BinIndex window_start_ = 0;
  BinIndex lowest_bin_ = 0;
  std::uint32_t bin_span_ = 0;
  // The lowest bin, once the range holds the maximum number of bins. Lower
  // values are moved up to it without a branch.
  BinIndex clamp_floor_ = std::numeric_limits<BinIndex>::min();
  std::uint64_t zero_count_ = 0;
};

}  // namespace datadog::telemetry
