#include "log_bin_index.h"

#include <array>
#include <cfenv>
#include <cmath>
#include <cstring>

namespace datadog::telemetry::log_bins {
namespace {

// Finds bins without computing a logarithm.
//
// A positive double is 2^e × (1 + f). The fraction f is in [0, 1) and is
// stored in 52 bits. The top 6 bits of f give an integer m from 0 to 63.
// This means f is in [m/64, (m + 1)/64). The exponent e and m form a key.
// For example, between 1 and 2, the key with m = 0 holds [1, 1.015625).
// The key with m = 1 holds [1.015625, 1.03125).
//
// A key's doubles go from 2^e × (1 + m/64) up to, but excluding,
// 2^e × (1 + (m + 1)/64). Its largest and smallest doubles satisfy
// largest/smallest < (65 + m)/(64 + m) <= 65/64.
// The fraction (65 + m)/(64 + m) equals 65/64 only when m = 0.
// So every key has largest/smallest strictly below 65/64.
// The bin ratio is 65/64. Neighbouring bin edges are 65/64 apart.
// So at most one bin edge falls inside a key. Its doubles are in one bin
// or in two neighbouring bins.
//
// This holds for the exact logarithm. The bins come from a rounded `log`,
// so two edges may be a few doubles less than 65/64 apart. When m = 0,
// a key spans almost exactly 65/64. If an edge fell just after its first
// double, the next edge could also fall inside the key. Its doubles would
// then be in three bins.
//
// fill_entry() checks that each key holds doubles in at most two neighbouring
// bins. If the check fails, every value takes the slow path.
//
// For each key, the table stores the bin of its first double and where the
// next bin starts. It stores that start as the remaining 46 mantissa bits.
// Finding a bin is then one load and one compare. The table is filled with
// reference_bin_index(), so the table and reference_bin_index() always give
// the same bin.
//
// The table covers [2^-30, 2^65). Other values take the slow path.
class BinTable {
 public:
  static const BinTable& instance();

  // Sets `bin` and returns true if the table covers the double `bits`.
  bool find_bin(std::uint64_t bits, BinIndex& bin) const;

  // False if the table could not be built, and every value takes the slow
  // path.
  bool usable() const { return usable_keys_ == key_count; }

 private:
  // A key is the exponent and the top 6 mantissa bits of a positive double.
  static constexpr int key_shift = 52 - 6;
  static constexpr std::uint64_t low_bits_mask =
      (std::uint64_t{1} << key_shift) - 1;
  static constexpr std::uint64_t first_key = std::uint64_t{1023 - 30} << 6;
  static constexpr std::uint64_t key_count = std::uint64_t{30 + 65} << 6;

  static constexpr int edge_bits = key_shift + 1;
  static constexpr int base_offset_bits = 64 - edge_bits;
  // An edge value past every low bits, for keys whose doubles are all in one
  // bin.
  static constexpr std::uint64_t no_edge = low_bits_mask + 1;

  struct Entry {
    // Low bits of the first double of the next bin, or `no_edge` if the next
    // bin starts after the key.
    std::uint64_t edge : edge_bits;
    // Bin of the key's first double, minus `first_bin_`.
    std::uint64_t base_offset : base_offset_bits;
  };
  static_assert(sizeof(Entry) == sizeof(std::uint64_t));

  BinTable();
  bool fill_entry(std::uint64_t key);

  BinIndex first_bin_;
  // `key_count`, or 0 if the table could not be built.
  std::uint64_t usable_keys_;
  std::array<Entry, key_count> entries_{};
};

// Sets round-to-nearest while it lives, then restores the caller's mode.
class RoundToNearest {
 public:
  RoundToNearest() : saved_mode_(std::fegetround()) {
    std::fesetround(FE_TONEAREST);
  }
  ~RoundToNearest() { std::fesetround(saved_mode_); }
  RoundToNearest(const RoundToNearest&) = delete;
  RoundToNearest& operator=(const RoundToNearest&) = delete;

 private:
  int saved_mode_;
};

double log_multiplier();
std::uint64_t find_edge(BinIndex bin, std::uint64_t below,
                        std::uint64_t at_or_above);
void narrow_around_guess(BinIndex bin, std::uint64_t& below,
                         std::uint64_t& at_or_above);
std::uint64_t to_bits(double value);
double from_bits(std::uint64_t bits);

}  // namespace

BinIndex bin_index(double value) {
  BinIndex bin;
  if (BinTable::instance().find_bin(to_bits(value), bin)) return bin;
  return reference_bin_index(value);
}

bool fast_table_built() { return BinTable::instance().usable(); }

BinIndex reference_bin_index(double value) {
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

// The table is built at run time, with the local `log`. C libraries do not
// all round `log` the same way, so a table built elsewhere could differ.
const BinTable& BinTable::instance() {
  static const BinTable table;
  return table;
}

bool BinTable::find_bin(std::uint64_t bits, BinIndex& bin) const {
  // Zero, negative values, NaN and infinities give keys out of range too.
  const std::uint64_t key = (bits >> key_shift) - first_key;
  if (key >= usable_keys_) return false;

  const Entry entry = entries_[key];
  const bool past_edge = (bits & low_bits_mask) >= entry.edge;
  bin = first_bin_ + static_cast<std::int32_t>(entry.base_offset) +
        static_cast<std::int32_t>(past_edge);
  return true;
}

// The first caller may have changed the rounding mode. The table must not
// depend on it.
BinTable::BinTable() : first_bin_(0), usable_keys_(key_count) {
  const RoundToNearest rounding;
  first_bin_ = reference_bin_index(from_bits(first_key << key_shift));
  for (std::uint64_t key = 0; key < key_count; ++key) {
    if (!fill_entry(key)) {
      usable_keys_ = 0;
      return;
    }
  }
}

bool BinTable::fill_entry(std::uint64_t key) {
  const std::uint64_t first_bits = (first_key + key) << key_shift;
  const std::uint64_t last_bits = first_bits | low_bits_mask;
  const BinIndex base_bin = reference_bin_index(from_bits(first_bits));
  const BinIndex last_bin = reference_bin_index(from_bits(last_bits));
  const std::int64_t base_offset = std::int64_t{base_bin} - first_bin_;
  if (base_offset < 0 || base_offset >> base_offset_bits != 0 ||
      last_bin < base_bin || last_bin > base_bin + 1) {
    return false;
  }

  Entry& entry = entries_[key];
  entry.base_offset = static_cast<std::uint64_t>(base_offset);
  entry.edge = no_edge;
  if (last_bin > base_bin) {
    entry.edge = find_edge(last_bin, first_bits, last_bits) - first_bits;
  }
  return true;
}

// Same formula as libdatadog: 1 / ln(bin_ratio).
double log_multiplier() {
  static const double multiplier = 1.0 / std::log(bin_ratio);
  return multiplier;
}

// Returns the bits of the smallest double whose bin is `bin`. Expects the bin
// of `below` to be lower than `bin`, and the bin of `at_or_above` not to be.
std::uint64_t find_edge(BinIndex bin, std::uint64_t below,
                        std::uint64_t at_or_above) {
  narrow_around_guess(bin, below, at_or_above);
  while (at_or_above - below > 1) {
    const std::uint64_t middle = below + (at_or_above - below) / 2;
    if (reference_bin_index(from_bits(middle)) >= bin) {
      at_or_above = middle;
    } else {
      below = middle;
    }
  }
  return at_or_above;
}

// Moves the bounds close to the edge computed with `exp`, so that the search
// needs few steps. The bounds keep their meaning.
void narrow_around_guess(BinIndex bin, std::uint64_t& below,
                         std::uint64_t& at_or_above) {
  const double guess_value =
      std::exp((bin - bin_index_shift) / log_multiplier());
  const std::uint64_t guess = to_bits(guess_value);
  if (guess <= below || guess >= at_or_above) return;

  for (std::uint64_t width = 64; width < at_or_above - below; width *= 64) {
    const std::uint64_t low = guess > width ? guess - width : 0;
    const std::uint64_t high = guess + width;
    if (low > below && reference_bin_index(from_bits(low)) < bin) {
      below = low;
    }
    if (high < at_or_above && reference_bin_index(from_bits(high)) >= bin) {
      at_or_above = high;
    }
    if (below == low && at_or_above == high) return;
  }
}

std::uint64_t to_bits(double value) {
  std::uint64_t bits;
  std::memcpy(&bits, &value, sizeof bits);
  return bits;
}

double from_bits(std::uint64_t bits) {
  double value;
  std::memcpy(&value, &bits, sizeof value);
  return value;
}

}  // namespace
}  // namespace datadog::telemetry::log_bins
