#include <benchmark/benchmark.h>
#include <datadog/trace_id.h>

#include "datadog/hex.h"

namespace {
namespace dd = datadog::tracing;

void BM_TraceID_HexPadded(benchmark::State& state) {
  const dd::TraceID id{0xDEADBEEFCAFEBABEULL, 0x0102030405060708ULL};
  for (auto _ : state) {
    auto result = id.hex_padded();
    benchmark::DoNotOptimize(result);
  }
}
BENCHMARK(BM_TraceID_HexPadded);

void BM_TraceID_ParseHex_128bit(benchmark::State& state) {
  const std::string input{"0102030405060708deadbeefcafebabe"};
  for (auto _ : state) {
    auto result = dd::TraceID::parse_hex(input);
    benchmark::DoNotOptimize(result);
  }
}
BENCHMARK(BM_TraceID_ParseHex_128bit);

void BM_TraceID_ParseHex_64bit(benchmark::State& state) {
  const std::string input{"deadbeefcafebabe"};
  for (auto _ : state) {
    auto result = dd::TraceID::parse_hex(input);
    benchmark::DoNotOptimize(result);
  }
}
BENCHMARK(BM_TraceID_ParseHex_64bit);

void BM_HexPadded_uint64(benchmark::State& state) {
  const std::uint64_t value = 0xDEADBEEFCAFEBABEULL;
  for (auto _ : state) {
    auto result = dd::hex_padded(value);
    benchmark::DoNotOptimize(result);
  }
}
BENCHMARK(BM_HexPadded_uint64);

void BM_Hex_uint64(benchmark::State& state) {
  const std::uint64_t value = 0xDEADBEEFCAFEBABEULL;
  for (auto _ : state) {
    auto result = dd::hex(value);
    benchmark::DoNotOptimize(result);
  }
}
BENCHMARK(BM_Hex_uint64);

}  // namespace
