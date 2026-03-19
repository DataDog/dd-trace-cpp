#pragma once

// This component provides a minimal DDSketch implementation for the
// Client-Side Stats feature.
//
// DDSketch is a mergeable, relative-error quantile sketch.  It maps values to
// bins using a logarithmic index scheme that guarantees at most `alpha`
// relative accuracy for any quantile query.
//
// This implementation is intentionally limited to what is needed by the stats
// concentrator: recording non-negative durations (in nanoseconds) and
// serializing the sketch to msgpack for the /v0.6/stats payload.
//
// Reference:
//   https://arxiv.org/abs/1908.10693

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace datadog {
namespace tracing {

class DDSketch {
 public:
  // Construct a DDSketch with the specified `relative_accuracy` (alpha) and
  // `max_num_bins` limit.  The default parameters match the Datadog Agent:
  //   - 1% relative accuracy (alpha = 0.01)
  //   - 2048 maximum bins
  explicit DDSketch(double relative_accuracy = 0.01,
                    std::size_t max_num_bins = 2048);

  // Insert the specified `value` (expected to be a non-negative duration in
  // nanoseconds).  Negative values are treated as zero.
  void add(double value);

  // Return the number of values that have been inserted.
  std::uint64_t count() const;

  // Return the sum of all values that have been inserted.
  double sum() const;

  // Return the minimum value that has been inserted, or 0 if empty.
  double min() const;

  // Return the maximum value that has been inserted, or 0 if empty.
  double max() const;

  // Return the average of all values that have been inserted, or 0 if empty.
  double avg() const;

  // Return whether the sketch is empty (no values inserted).
  bool empty() const;

  // Reset the sketch to its initial state.
  void clear();

  // Append the msgpack-encoded sketch to `destination`.  The encoding follows
  // the proto/v0.6 stats payload format (DDSketch proto message):
  //   - mapping: {gamma, index_offset, interpolation}
  //   - positive_values: {contiguous_bin_counts, contiguous_bin_index_offset}
  //   - zero_count
  void msgpack_encode(std::string& destination) const;

 private:
  // Return the bin index for the specified `value`.
  int key(double value) const;

  // Return the lower bound value of the bin at the specified `index`.
  double lower_bound(int index) const;

  double gamma_;
  double log_gamma_;
  std::size_t max_num_bins_;

  // Contiguous bins stored from min_key_ onward.
  // bins_[i] = count for key (min_key_ + i).
  std::vector<std::uint64_t> bins_;
  int min_key_ = 0;

  std::uint64_t count_ = 0;
  std::uint64_t zero_count_ = 0;
  double sum_ = 0.0;
  double min_ = 0.0;
  double max_ = 0.0;
};

}  // namespace tracing
}  // namespace datadog
