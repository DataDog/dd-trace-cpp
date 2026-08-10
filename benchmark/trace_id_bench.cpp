#include <benchmark/benchmark.h>
#include <datadog/trace_id.h>

#include "datadog/hex.h"

namespace {
namespace dd = datadog::tracing;

void BM_TraceID_HexPadded(benchmark::State& state, dd::TraceID id) {
  for (auto _ : state) {
    auto result = id.hex_padded();
    benchmark::DoNotOptimize(result);
  }
}
BENCHMARK_CAPTURE(BM_TraceID_HexPadded, NoPadding,
                  dd::TraceID{0xDEADBEEFCAFEBABEULL, 0x0102030405060708ULL});
BENCHMARK_CAPTURE(BM_TraceID_HexPadded, WithPadding, dd::TraceID{1, 0});

void BM_TraceID_ParseHex(benchmark::State& state, std::string input) {
  for (auto _ : state) {
    auto result = dd::TraceID::parse_hex(input);
    benchmark::DoNotOptimize(result);
  }
}
BENCHMARK_CAPTURE(BM_TraceID_ParseHex, 64bit, std::string{"deadbeefcafebabe"});
BENCHMARK_CAPTURE(BM_TraceID_ParseHex, 128bit,
                  std::string{"0102030405060708deadbeefcafebabe"});

void BM_HexPadded_uint64(benchmark::State& state, std::uint64_t value) {
  for (auto _ : state) {
    auto result = dd::hex_padded(value);
    benchmark::DoNotOptimize(result);
  }
}
BENCHMARK_CAPTURE(BM_HexPadded_uint64, NoPadding, 0xDEADBEEFCAFEBABEULL);
BENCHMARK_CAPTURE(BM_HexPadded_uint64, WorstCasePadding, 0x1ULL);

void BM_Hex_uint64(benchmark::State& state) {
  const std::uint64_t value = 0xDEADBEEFCAFEBABEULL;
  for (auto _ : state) {
    auto result = dd::hex(value);
    benchmark::DoNotOptimize(result);
  }
}
BENCHMARK(BM_Hex_uint64);

}  // namespace
