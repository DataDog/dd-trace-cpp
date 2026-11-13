#include <algorithm>
#include <cfloat>
#include <cmath>
#include <cstring>
#include <functional>
#include <map>
#include <random>
#include <thread>
#include <vector>

#include "datadog/telemetry/log_bin_index.h"
#include "datadog/telemetry/log_binned_distr.h"
#include "test.h"

using namespace datadog::telemetry;

#define LOG_BINNED_TEST(x) TEST_CASE(x, "[telemetry][log_binned_distr]")

namespace {

using BinIndex = log_bins::BinIndex;
using Bins = std::map<BinIndex, double>;

struct DecodedSketch {
  double gamma = 0;
  double index_offset = 0;
  std::uint64_t interpolation = 0;
  Bins bins;
  bool has_negative_store = false;
  Bins negative_bins;
  double zero_count = 0;
};

DecodedSketch decode_sketch(const std::string& data);
std::uint64_t to_bits(double value);
double from_bits(std::uint64_t bits);

// The bins expected once `values` are recorded. Each value counts in its own
// bin, but only the 2048 highest bins are kept. Lower values count in the
// lowest bin kept.
Bins expected_bins(const std::vector<double>& values) {
  Bins exact;
  for (const double value : values) {
    const BinIndex bin = log_bins::bin_index(value);
    if (bin != log_bins::zero_bin) exact[bin] += 1;
  }
  if (exact.empty()) return exact;

  const BinIndex lowest_kept = exact.rbegin()->first - 2047;
  Bins expected;
  for (const auto& [bin, count] : exact) {
    expected[std::max(bin, lowest_kept)] += count;
  }
  return expected;
}

// A value in the middle of `bin`, half a bin from both edges. Bin 1338 holds
// 1.0.
double value_in_bin(BinIndex bin) {
  return std::pow(log_bins::bin_ratio, bin - 1338);
}

Bins recorded_bins(const std::vector<BinIndex>& bins) {
  LogBinnedDistr distribution;
  for (const BinIndex bin : bins) distribution.record(value_in_bin(bin));
  return decode_sketch(distribution.encode_protobuf()).bins;
}

// Returns the bits of the smallest double whose reference bin is at least
// `bin`, searching in (below, at_or_above].
std::uint64_t reference_edge(BinIndex bin, std::uint64_t below,
                             std::uint64_t at_or_above) {
  while (at_or_above - below > 1) {
    const std::uint64_t middle = below + (at_or_above - below) / 2;
    if (log_bins::reference_bin_index(from_bits(middle)) >= bin) {
      at_or_above = middle;
    } else {
      below = middle;
    }
  }
  return at_or_above;
}

void check_same_bin(double value) {
  INFO("value " << value << ", bits " << to_bits(value));
  CHECK(log_bins::bin_index(value) == log_bins::reference_bin_index(value));
}

void check_bins(const std::vector<double>& values) {
  LogBinnedDistr distribution;
  for (const double value : values) distribution.record(value);
  const double zero_count =
      static_cast<double>(std::count(values.begin(), values.end(), 0.0));

  const DecodedSketch decoded = decode_sketch(distribution.encode_protobuf());
  CHECK(decoded.bins == expected_bins(values));
  CHECK(decoded.zero_count == zero_count);
}

std::vector<double> log_uniform(std::size_t count, double lowest,
                                double highest, std::uint64_t seed) {
  std::mt19937_64 generator(seed);
  std::uniform_real_distribution<double> exponent(std::log(lowest),
                                                  std::log(highest));
  std::vector<double> values(count);
  for (double& value : values) value = std::exp(exponent(generator));
  return values;
}

}  // namespace

LOG_BINNED_TEST("mapping constants follow libdatadog's formulas") {
  const double relative_accuracy = 0.007751937984496124;
  CHECK((1 + relative_accuracy) / (1 - relative_accuracy) ==
        log_bins::bin_ratio);

  const double log_ratio = std::log(log_bins::bin_ratio);
  CHECK((1 - std::floor(std::log(1e-9) / log_ratio)) + 0.5 ==
        log_bins::bin_index_shift);

  const double multiplier = 1 / log_ratio;
  const double int32_limit =
      std::exp((INT32_MIN - log_bins::bin_index_shift) / multiplier + 1.0);
  CHECK(std::max(DBL_MIN * log_bins::bin_ratio, int32_limit) ==
        log_bins::zero_threshold);
}

LOG_BINNED_TEST("bin index of special values") {
  const double infinity = std::numeric_limits<double>::infinity();
  const std::vector<std::pair<double, BinIndex>> cases{
      {std::nan(""), log_bins::rejected_value},
      {infinity, log_bins::rejected_value},
      {-infinity, log_bins::rejected_value},
      {-1.0, log_bins::rejected_value},
      {-DBL_MIN, log_bins::rejected_value},
      {0.0, log_bins::zero_bin},
      {-0.0, log_bins::zero_bin},
      {std::numeric_limits<double>::denorm_min(), log_bins::zero_bin},
      {DBL_MIN, log_bins::zero_bin},
      {std::nextafter(log_bins::zero_threshold, 0.0), log_bins::zero_bin},
      {log_bins::zero_threshold, -44352},
      {1e-9, 1},
      {1.0, 1338},
      {1000.0, 1784},
      {DBL_MAX, 47118},
  };
  for (const auto& [value, expected_bin] : cases) {
    INFO("value " << value);
    CHECK(log_bins::bin_index(value) == expected_bin);
    CHECK(log_bins::reference_bin_index(value) == expected_bin);
  }
}

LOG_BINNED_TEST("the fast table is built") {
  CHECK(log_bins::fast_table_built());
}

LOG_BINNED_TEST("bin index of values far from an edge is exact") {
  // Exact bins, computed with 300-bit arithmetic (mpmath): the floor of
  // ln(x) / ln(65/64) + 1338.5. Each value is at least 0.04 bins from an edge.
  const std::vector<std::pair<double, BinIndex>> cases{
      {1e-300, -43216}, {5e-300, -43112}, {1e-20, -1632}, {3.3e-15, -813},
      {1e-9, 1},        {0.001, 892},     {0.37, 1274},   {0.5, 1293},
      {1.0, 1338},      {2.5, 1397},      {3.0, 1409},    {123.456, 1649},
      {1000.0, 1784},   {65536.0, 2053},  {1e9, 2675},    {1e12, 3120},
      {1e18, 4011},     {1e25, 5051},     {1e100, 16189}, {1e300, 45892},
  };
  for (const auto& [value, expected_bin] : cases) {
    INFO("value " << value);
    CHECK(log_bins::bin_index(value) == expected_bin);
  }
}

LOG_BINNED_TEST("bin index rounds the multiplication and the addition once") {
  // ln(x) / ln(65/64) + 1338.5 is 3253.99999999999995516 for this x, so the
  // exact bin is 3253. Rounding the multiplication first would round the
  // result up to 3254.
  const double value = 7'903'045'517'381.0;
  CHECK(log_bins::reference_bin_index(value) == 3253);
  CHECK(log_bins::bin_index(value) == 3253);
}

LOG_BINNED_TEST("bin index equals the reference around every bin edge") {
  // Covers the fast table, [2^-30, 2^65), and some bins on each side.
  const std::uint64_t lowest_bits = to_bits(std::ldexp(1.0, -32));
  const std::uint64_t highest_bits = to_bits(std::ldexp(1.0, 67));
  const BinIndex first_bin =
      log_bins::reference_bin_index(from_bits(lowest_bits));
  const BinIndex last_bin =
      log_bins::reference_bin_index(from_bits(highest_bits));

  std::uint64_t previous_edge = lowest_bits;
  for (BinIndex bin = first_bin + 1; bin <= last_bin; ++bin) {
    const std::uint64_t edge = reference_edge(bin, previous_edge, highest_bits);
    for (std::uint64_t step = 0; step <= 6; ++step) {
      check_same_bin(from_bits(edge - 3 + step));
    }
    previous_edge = edge;
  }
}

LOG_BINNED_TEST("bin index equals the reference on many values") {
  SECTION("log-uniform values") {
    for (const double value : log_uniform(1'000'000, 1e-15, 1e25, 1)) {
      check_same_bin(value);
    }
  }
  SECTION("random bit patterns") {
    std::mt19937_64 generator(2);
    for (int count = 0; count < 1'000'000; ++count) {
      check_same_bin(from_bits(generator()));
    }
  }
  SECTION("integers and decimal fractions") {
    for (int number = 0; number < 200'000; ++number) {
      check_same_bin(number);
      check_same_bin(number / 1000.0);
    }
  }
}

LOG_BINNED_TEST("record rejects invalid values") {
  LogBinnedDistr distribution;
  CHECK_FALSE(distribution.record(std::nan("")));
  CHECK_FALSE(distribution.record(-1.0));
  CHECK_FALSE(distribution.record(std::numeric_limits<double>::infinity()));
  CHECK(distribution.record(0.0));
  CHECK(distribution.record(-0.0));

  const DecodedSketch decoded = decode_sketch(distribution.encode_protobuf());
  CHECK(decoded.bins.empty());
  CHECK(decoded.zero_count == 2);
}

LOG_BINNED_TEST("encoding of an empty distribution") {
  const LogBinnedDistr distribution;
  const unsigned char expected[] = {
      0x0a, 0x12, 0x09, 0x00, 0x00, 0x00, 0x00, 0x00, 0x40, 0xf0, 0x3f, 0x11,
      0x00, 0x00, 0x00, 0x00, 0x00, 0xea, 0x94, 0x40, 0x12, 0x00, 0x1a, 0x00};
  CHECK(distribution.encode_protobuf() ==
        std::string(reinterpret_cast<const char*>(expected), sizeof expected));
}

LOG_BINNED_TEST("encoding of one value") {
  LogBinnedDistr distribution;
  distribution.record(1.0);
  const unsigned char expected[] = {
      0x0a, 0x12, 0x09, 0x00, 0x00, 0x00, 0x00, 0x00, 0x40, 0xf0,
      0x3f, 0x11, 0x00, 0x00, 0x00, 0x00, 0x00, 0xea, 0x94, 0x40,
      0x12, 0x0e, 0x0a, 0x0c, 0x08, 0xf4, 0x14, 0x11, 0x00, 0x00,
      0x00, 0x00, 0x00, 0x00, 0xf0, 0x3f, 0x1a, 0x00};
  CHECK(distribution.encode_protobuf() ==
        std::string(reinterpret_cast<const char*>(expected), sizeof expected));

  const DecodedSketch decoded = decode_sketch(distribution.encode_protobuf());
  CHECK(decoded.gamma == 65.0 / 64.0);
  CHECK(decoded.index_offset == 1338.5);
  CHECK(decoded.interpolation == 0);
  CHECK(decoded.bins == Bins{{1338, 1.0}});
  CHECK(decoded.has_negative_store);
  CHECK(decoded.negative_bins.empty());
}

LOG_BINNED_TEST("the store keeps the 2048 highest bins") {
  SECTION("a range of 2048 bins keeps every bin") {
    CHECK(recorded_bins({1000, 3047}) == Bins{{1000, 1}, {3047, 1}});
  }
  SECTION("a higher value merges the lowest bins into the lowest bin kept") {
    CHECK(recorded_bins({1000, 1000, 1500, 3048}) ==
          Bins{{1001, 2}, {1500, 1}, {3048, 1}});
    CHECK(recorded_bins({1000, 1500, 9000}) == Bins{{6953, 2}, {9000, 1}});
  }
  SECTION("a lower value goes to the lowest bin kept") {
    CHECK(recorded_bins({3047, 1000, 999, 10}) == Bins{{1000, 3}, {3047, 1}});
    CHECK(recorded_bins({3047, 500}) == Bins{{1000, 1}, {3047, 1}});
    CHECK(recorded_bins({1000, 3048, 5}) == Bins{{1001, 2}, {3048, 1}});
  }
}

LOG_BINNED_TEST("bins of many values") {
  SECTION("narrow durations") {
    check_bins(log_uniform(100'000, 100, 10'000, 3));
  }
  SECTION("wide range, which collapses the lowest bins") {
    check_bins(log_uniform(100'000, 1e-12, 1e22, 4));
  }
  SECTION("values that only increase") {
    std::vector<double> values = log_uniform(20'000, 1e-9, 1e20, 5);
    std::sort(values.begin(), values.end());
    check_bins(values);
  }
  SECTION("values that only decrease") {
    std::vector<double> values = log_uniform(20'000, 1e-9, 1e20, 6);
    std::sort(values.begin(), values.end(), std::greater<double>());
    check_bins(values);
  }
  SECTION("values alternating between both ends") {
    std::vector<double> values;
    for (int step = 0; step < 5'000; ++step) {
      values.push_back(1e-6 * (1 + step % 7));
      values.push_back(1e15 * (1 + step % 5));
    }
    check_bins(values);
  }
  SECTION("low values, then high ones, then low ones again") {
    std::vector<double> values = log_uniform(5'000, 1, 100, 7);
    const std::vector<double> high = log_uniform(5'000, 1e14, 1e16, 8);
    const std::vector<double> low = log_uniform(5'000, 1e-3, 10, 9);
    values.insert(values.end(), high.begin(), high.end());
    values.insert(values.end(), low.begin(), low.end());
    check_bins(values);
  }
  SECTION("zeros and fractions mixed") {
    std::vector<double> values = log_uniform(50'000, 1e-4, 1, 10);
    for (std::size_t index = 0; index < values.size(); index += 10) {
      values[index] = 0;
    }
    check_bins(values);
  }
}

LOG_BINNED_TEST("records from several threads are all counted") {
  LogBinnedDistr distribution;
  static constexpr int thread_count = 4;
  static constexpr int values_per_thread = 50'000;
  std::vector<std::thread> threads;
  for (int thread_number = 0; thread_number < thread_count; ++thread_number) {
    threads.emplace_back([&distribution, thread_number] {
      for (int index = 0; index < values_per_thread; ++index) {
        distribution.record(1.0 + thread_number + index % 100);
      }
    });
  }
  for (std::thread& thread : threads) thread.join();

  double total = 0;
  for (const auto& [bin, count] :
       decode_sketch(distribution.encode_protobuf()).bins) {
    total += count;
  }
  CHECK(total == thread_count * values_per_thread);
}

namespace {

// A minimal protobuf reader, for the messages of a DDSketch.
class ProtobufReader {
 public:
  explicit ProtobufReader(const std::string& data) : data_(data) {}

  bool done() const { return position_ >= data_.size(); }

  std::uint64_t read_varint() {
    std::uint64_t value = 0;
    for (int shift = 0;; shift += 7) {
      const auto byte = static_cast<unsigned char>(data_.at(position_++));
      value |= std::uint64_t{byte & 0x7fu} << shift;
      if ((byte & 0x80) == 0) return value;
    }
  }

  double read_double() {
    std::uint64_t bits = 0;
    for (int byte = 0; byte < 8; ++byte) {
      const auto value = static_cast<unsigned char>(data_.at(position_++));
      bits |= std::uint64_t{value} << (8 * byte);
    }
    return from_bits(bits);
  }

  std::string read_bytes() {
    const std::size_t length = read_varint();
    REQUIRE(position_ + length <= data_.size());
    std::string bytes = data_.substr(position_, length);
    position_ += length;
    return bytes;
  }

 private:
  const std::string& data_;
  std::size_t position_ = 0;
};

std::int32_t unzigzag(std::uint64_t value) {
  const auto low = static_cast<std::uint32_t>(value);
  return static_cast<std::int32_t>((low >> 1) ^ (0u - (low & 1)));
}

void decode_bin(const std::string& data, Bins& bins) {
  ProtobufReader reader(data);
  BinIndex bin = 0;
  double count = 0;
  while (!reader.done()) {
    const std::uint64_t tag = reader.read_varint();
    if (tag == ((1 << 3) | 0)) {
      bin = unzigzag(reader.read_varint());
    } else if (tag == ((2 << 3) | 1)) {
      count = reader.read_double();
    } else {
      FAIL("unexpected field in a bin entry: " << tag);
    }
  }
  bins[bin] += count;
}

Bins decode_store(const std::string& data) {
  ProtobufReader reader(data);
  Bins bins;
  while (!reader.done()) {
    const std::uint64_t tag = reader.read_varint();
    REQUIRE(tag == ((1 << 3) | 2));
    decode_bin(reader.read_bytes(), bins);
  }
  return bins;
}

void decode_mapping(const std::string& data, DecodedSketch& sketch) {
  ProtobufReader reader(data);
  while (!reader.done()) {
    const std::uint64_t tag = reader.read_varint();
    if (tag == ((1 << 3) | 1)) {
      sketch.gamma = reader.read_double();
    } else if (tag == ((2 << 3) | 1)) {
      sketch.index_offset = reader.read_double();
    } else if (tag == ((3 << 3) | 0)) {
      sketch.interpolation = reader.read_varint();
    } else {
      FAIL("unexpected field in the mapping: " << tag);
    }
  }
}

DecodedSketch decode_sketch(const std::string& data) {
  ProtobufReader reader(data);
  DecodedSketch sketch;
  while (!reader.done()) {
    const std::uint64_t tag = reader.read_varint();
    if (tag == ((1 << 3) | 2)) {
      decode_mapping(reader.read_bytes(), sketch);
    } else if (tag == ((2 << 3) | 2)) {
      sketch.bins = decode_store(reader.read_bytes());
    } else if (tag == ((3 << 3) | 2)) {
      sketch.has_negative_store = true;
      sketch.negative_bins = decode_store(reader.read_bytes());
    } else if (tag == ((4 << 3) | 1)) {
      sketch.zero_count = reader.read_double();
    } else {
      FAIL("unexpected field in the sketch: " << tag);
    }
  }
  return sketch;
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
