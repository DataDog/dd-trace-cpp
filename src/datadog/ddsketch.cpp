#include "ddsketch.h"

#include <algorithm>
#include <cassert>
#include <cmath>
#include <limits>

#include "msgpack.h"

namespace datadog {
namespace tracing {

DDSketch::DDSketch(double relative_accuracy, std::size_t max_num_bins)
    : max_num_bins_(max_num_bins) {
  assert(relative_accuracy > 0.0 && relative_accuracy < 1.0);
  assert(max_num_bins > 0);

  // gamma = (1 + alpha) / (1 - alpha)
  gamma_ = (1.0 + relative_accuracy) / (1.0 - relative_accuracy);
  log_gamma_ = std::log(gamma_);
}

int DDSketch::key(double value) const {
  // Map value to a bin index using log(value) / log(gamma).
  // The index is ceiling of log_gamma(value).
  return static_cast<int>(std::ceil(std::log(value) / log_gamma_));
}

double DDSketch::lower_bound(int index) const {
  return std::pow(gamma_, index - 1);
}

void DDSketch::add(double value) {
  if (value < 0.0) {
    value = 0.0;
  }

  count_++;
  sum_ += value;

  if (count_ == 1) {
    min_ = value;
    max_ = value;
  } else {
    if (value < min_) min_ = value;
    if (value > max_) max_ = value;
  }

  // Values that are effectively zero go into the zero bucket.
  // Using a threshold comparable to the smallest representable bin.
  constexpr double min_positive = 1e-9;  // 1 nanosecond
  if (value <= min_positive) {
    zero_count_++;
    return;
  }

  int k = key(value);

  if (bins_.empty()) {
    min_key_ = k;
    bins_.push_back(1);
    return;
  }

  // Expand bins_ to cover the new key.
  if (k < min_key_) {
    // Prepend bins.
    int prepend = min_key_ - k;
    bins_.insert(bins_.begin(), static_cast<std::size_t>(prepend), 0);
    min_key_ = k;
    bins_[0] = 1;
  } else if (k >= min_key_ + static_cast<int>(bins_.size())) {
    // Append bins.
    bins_.resize(static_cast<std::size_t>(k - min_key_ + 1), 0);
    bins_[static_cast<std::size_t>(k - min_key_)] = 1;
  } else {
    bins_[static_cast<std::size_t>(k - min_key_)]++;
  }

  // If we have exceeded the max number of bins, collapse by merging
  // adjacent bins (pairs from the left).
  while (bins_.size() > max_num_bins_) {
    std::vector<std::uint64_t> merged;
    merged.reserve((bins_.size() + 1) / 2);
    for (std::size_t i = 0; i < bins_.size(); i += 2) {
      std::uint64_t c = bins_[i];
      if (i + 1 < bins_.size()) {
        c += bins_[i + 1];
      }
      merged.push_back(c);
    }
    // After merging pairs, the new min_key_ corresponds to
    // the original min_key_ / 2 (floor division for the mapping).
    // Actually, for log-based indexing, merging adjacent bins means
    // each new bin covers twice the range, which is equivalent to
    // halving the resolution.  For simplicity, we just reassign.
    min_key_ = min_key_ / 2;
    bins_ = std::move(merged);
  }
}

std::uint64_t DDSketch::count() const { return count_; }

double DDSketch::sum() const { return sum_; }

double DDSketch::min() const { return count_ > 0 ? min_ : 0.0; }

double DDSketch::max() const { return count_ > 0 ? max_ : 0.0; }

double DDSketch::avg() const {
  return count_ > 0 ? sum_ / static_cast<double>(count_) : 0.0;
}

bool DDSketch::empty() const { return count_ == 0; }

void DDSketch::clear() {
  bins_.clear();
  min_key_ = 0;
  count_ = 0;
  zero_count_ = 0;
  sum_ = 0.0;
  min_ = 0.0;
  max_ = 0.0;
}

void DDSketch::msgpack_encode(std::string& destination) const {
  // The DDSketch proto message has 3 fields:
  //   mapping: {gamma, index_offset, interpolation}
  //   positive_values: {contiguous_bin_counts, contiguous_bin_index_offset}
  //   zero_count
  //
  // We encode this as a msgpack map.

  // Mapping sub-map
  // interpolation: 0 = NONE (logarithmic)
  // index_offset: 0 (we use the raw index)

  // clang-format off
  msgpack::pack_map(destination, 3);

  // 1) "mapping"
  msgpack::pack_string(destination, "mapping");
  msgpack::pack_map(destination, 3);
  msgpack::pack_string(destination, "gamma");
  msgpack::pack_double(destination, gamma_);
  msgpack::pack_string(destination, "indexOffset");
  msgpack::pack_double(destination, 0.0);
  msgpack::pack_string(destination, "interpolation");
  msgpack::pack_integer(destination, std::int64_t(0));  // NONE

  // 2) "positiveValues"
  msgpack::pack_string(destination, "positiveValues");
  msgpack::pack_map(destination, 2);
  msgpack::pack_string(destination, "contiguousBinCounts");
  // Pack as array of doubles (matching the proto float64 repeated field).
  msgpack::pack_array(destination, bins_.size());
  for (auto c : bins_) {
    msgpack::pack_double(destination, static_cast<double>(c));
  }
  msgpack::pack_string(destination, "contiguousBinIndexOffset");
  msgpack::pack_integer(destination, std::int64_t(min_key_));

  // 3) "zeroCount"
  msgpack::pack_string(destination, "zeroCount");
  msgpack::pack_double(destination, static_cast<double>(zero_count_));
  // clang-format on
}

}  // namespace tracing
}  // namespace datadog
