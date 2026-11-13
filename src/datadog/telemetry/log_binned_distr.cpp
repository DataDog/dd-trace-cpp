#include "log_binned_distr.h"

#include <algorithm>
#include <cstring>
#include <type_traits>

#include "log_bin_index.h"

namespace datadog::telemetry {
namespace {

using BinIndex = log_bins::BinIndex;

constexpr std::int32_t max_bins = 2048;
constexpr BinIndex no_clamp = std::numeric_limits<BinIndex>::min();
constexpr std::uint32_t min_window_size = 64;
constexpr std::uint32_t max_window_size = 2 * max_bins;

std::string encode_mapping();
std::string encode_bin(BinIndex bin, std::uint64_t count);
void append_message(std::string& out, std::uint32_t field,
                    const std::string& message);
void append_double(std::string& out, std::uint32_t field, double value);
void append_varint(std::string& out, std::uint64_t value);
std::uint32_t zigzag(std::int32_t value);

}  // namespace

bool LogBinnedDistr::record(double value) {
  static_assert(std::is_same_v<BinIndex, log_bins::BinIndex>);
  const BinIndex bin = log_bins::bin_index(value);
  if (bin == log_bins::rejected_value) return false;

  std::lock_guard<std::mutex> lock(mutex_);
  if (bin == log_bins::zero_bin) {
    ++zero_count_;
  } else {
    add_to_bin(bin);
  }
  return true;
}

// DDSketch message: mapping = 1, positiveValues = 2, negativeValues = 3,
// zeroCount = 4.
std::string LogBinnedDistr::encode_protobuf() const {
  std::lock_guard<std::mutex> lock(mutex_);
  std::string sketch;
  append_message(sketch, 1, encode_mapping());
  append_message(sketch, 2, encode_bins());
  append_message(sketch, 3, "");
  if (zero_count_ != 0) {
    append_double(sketch, 4, static_cast<double>(zero_count_));
  }
  return sketch;
}

void LogBinnedDistr::add_to_bin(BinIndex bin) {
  bin = std::max(bin, clamp_floor_);
  const std::uint32_t position =
      static_cast<std::uint32_t>(bin) - static_cast<std::uint32_t>(lowest_bin_);
  if (position < bin_span_) {
    ++count_at(bin);
    return;
  }
  extend_range(bin);
  const bool full = bin_span_ == static_cast<std::uint32_t>(max_bins);
  clamp_floor_ = full ? lowest_bin_ : no_clamp;
}

// Same rules as libdatadog's LowCollapsingDenseStore.
void LogBinnedDistr::extend_range(BinIndex bin) {
  if (bin_span_ == 0) {
    start_range(bin);
    return;
  }
  const BinIndex highest =
      lowest_bin_ + static_cast<std::int32_t>(bin_span_) - 1;
  if (bin < lowest_bin_) {
    extend_down(bin, highest);
  } else {
    extend_up(bin, highest);
  }
}

void LogBinnedDistr::start_range(BinIndex bin) {
  cover(bin, bin);
  lowest_bin_ = bin;
  bin_span_ = 1;
  ++count_at(bin);
}

// The range grows down while it has room. Values below it go to its lowest
// bin.
void LogBinnedDistr::extend_down(BinIndex bin, BinIndex highest) {
  const BinIndex new_lowest = std::max(bin, highest - (max_bins - 1));
  cover(new_lowest, highest);
  lowest_bin_ = new_lowest;
  bin_span_ = static_cast<std::uint32_t>(highest - new_lowest + 1);
  ++count_at(new_lowest);
}

// The range grows up. If it gets too wide, its lowest bins are merged into
// the new lowest bin.
void LogBinnedDistr::extend_up(BinIndex bin, BinIndex highest) {
  const BinIndex new_lowest = std::max(lowest_bin_, bin - (max_bins - 1));
  const std::uint64_t merged =
      take_counts(lowest_bin_, std::min(new_lowest - 1, highest));
  cover(new_lowest, bin);
  lowest_bin_ = new_lowest;
  bin_span_ = static_cast<std::uint32_t>(bin - new_lowest + 1);
  count_at(new_lowest) += merged;
  ++count_at(bin);
}

// Sets the counts of bins [lowest, highest] to zero and returns their sum.
std::uint64_t LogBinnedDistr::take_counts(BinIndex lowest, BinIndex highest) {
  std::uint64_t sum = 0;
  for (BinIndex bin = lowest; bin <= highest; ++bin) {
    std::uint64_t& count = count_at(bin);
    sum += count;
    count = 0;
  }
  return sum;
}

// Makes the window cover bins [lowest, highest]. The new window has room on
// both sides, so that the range can grow without moving it each time.
void LogBinnedDistr::cover(BinIndex lowest, BinIndex highest) {
  const BinIndex window_end =
      window_start_ + static_cast<std::int32_t>(counts_.size());
  if (!counts_.empty() && lowest >= window_start_ && highest < window_end) {
    return;
  }

  const std::uint32_t needed = static_cast<std::uint32_t>(highest - lowest + 1);
  const std::uint32_t size =
      std::clamp(2 * needed, min_window_size, max_window_size);
  const BinIndex start =
      lowest - static_cast<std::int32_t>((size - needed) / 2);
  std::vector<std::uint64_t> counts(size);
  const BinIndex copy_start = std::max(window_start_, start);
  const BinIndex copy_end =
      std::min(window_end, start + static_cast<std::int32_t>(size));
  for (BinIndex bin = copy_start; bin < copy_end; ++bin) {
    counts[static_cast<std::size_t>(bin - start)] = count_at(bin);
  }
  counts_.swap(counts);
  window_start_ = start;
}

std::uint64_t& LogBinnedDistr::count_at(BinIndex bin) {
  return counts_[static_cast<std::size_t>(bin - window_start_)];
}

// Store message: binCounts = 1 (map<sint32, double>), with only the bins that
// are not empty.
std::string LogBinnedDistr::encode_bins() const {
  std::string store;
  for (std::uint32_t position = 0; position < bin_span_; ++position) {
    const BinIndex bin = lowest_bin_ + static_cast<std::int32_t>(position);
    const std::uint64_t count =
        counts_[static_cast<std::size_t>(bin - window_start_)];
    if (count != 0) append_message(store, 1, encode_bin(bin, count));
  }
  return store;
}

namespace {

// IndexMapping message: gamma = 1, indexOffset = 2. The interpolation is
// NONE, the default, so it is left out.
std::string encode_mapping() {
  std::string mapping;
  append_double(mapping, 1, log_bins::bin_ratio);
  append_double(mapping, 2, log_bins::bin_index_shift);
  return mapping;
}

// Map entry message: key = 1 (sint32), value = 2 (double).
std::string encode_bin(BinIndex bin, std::uint64_t count) {
  std::string entry;
  append_varint(entry, (1 << 3) | 0);
  append_varint(entry, zigzag(bin));
  append_double(entry, 2, static_cast<double>(count));
  return entry;
}

void append_message(std::string& out, std::uint32_t field,
                    const std::string& message) {
  append_varint(out, (field << 3) | 2);
  append_varint(out, message.size());
  out += message;
}

void append_double(std::string& out, std::uint32_t field, double value) {
  append_varint(out, (field << 3) | 1);
  std::uint64_t bits;
  std::memcpy(&bits, &value, sizeof bits);
  for (int byte = 0; byte < 8; ++byte) {
    out.push_back(static_cast<char>((bits >> (8 * byte)) & 0xff));
  }
}

void append_varint(std::string& out, std::uint64_t value) {
  while (value >= 0x80) {
    out.push_back(static_cast<char>((value & 0x7f) | 0x80));
    value >>= 7;
  }
  out.push_back(static_cast<char>(value));
}

std::uint32_t zigzag(std::int32_t value) {
  const std::uint32_t shifted = static_cast<std::uint32_t>(value) << 1;
  return value < 0 ? ~shifted : shifted;
}

}  // namespace
}  // namespace datadog::telemetry
