#include <datadog/rate.h>
#include <datadog/sampling_util.h>

#include <cstdint>
#include <string>

#include "test.h"

using namespace datadog::tracing;

TEST_CASE("probabilistic sampling") {
  struct TestCase {
    int line;
    std::string name;
    std::uint64_t id;    // maybe the lower 64 of a trace ID, maybe a span ID
    Rate probability;    // i.e. the sample rate
    bool expected_keep;  // true: expected keep, false: expected drop
  };

  // clang-format off
    auto test_case = GENERATE(values<TestCase>({
        {__LINE__, "Test very small traceID", UINT64_C(1), *Rate::from(0.5), true},
        {__LINE__, "Test very small traceID", UINT64_C(10), *Rate::from(0.5), false},
        {__LINE__, "Test very small traceID", UINT64_C(100), *Rate::from(0.5), true},
        {__LINE__, "Test very small traceID", UINT64_C(1000), *Rate::from(0.5), true},
        {__LINE__, "Test random very large traceID", UINT64_C(18444899399302180860), *Rate::from(0.5), false},
        {__LINE__, "Test random very large traceID", UINT64_C(18444899399302180861), *Rate::from(0.5), false},
        {__LINE__, "Test random very large traceID", UINT64_C(18444899399302180862), *Rate::from(0.5), true},
        {__LINE__, "Test random very large traceID", UINT64_C(18444899399302180863), *Rate::from(0.5), true},
        {__LINE__, "Test the maximum traceID value 2**64-1", UINT64_C(18446744073709551615), *Rate::from(0.5), false},
        {__LINE__, "Test 2**63+1", UINT64_C(9223372036854775809), *Rate::from(0.5), false},
        {__LINE__, "Test 2**63-1", UINT64_C(9223372036854775807), *Rate::from(0.5), true},
        {__LINE__, "Test 2**62+1", UINT64_C(4611686018427387905), *Rate::from(0.5), false},
        {__LINE__, "Test 2**62-1", UINT64_C(4611686018427387903), *Rate::from(0.5), false},
        {__LINE__, "10 random traceIDs", UINT64_C(646771306295669658), *Rate::from(0.5), true},
        {__LINE__, "10 random traceIDs", UINT64_C(1882305164521835798), *Rate::from(0.5), true},
        {__LINE__, "10 random traceIDs", UINT64_C(5198373796167680436), *Rate::from(0.5), false},
        {__LINE__, "10 random traceIDs", UINT64_C(6272545487220484606), *Rate::from(0.5), true},
        {__LINE__, "10 random traceIDs", UINT64_C(8696342848850656916), *Rate::from(0.5), true},
        {__LINE__, "10 random traceIDs", UINT64_C(10197320802478874805), *Rate::from(0.5), true},
        {__LINE__, "10 random traceIDs", UINT64_C(10350218024687037124), *Rate::from(0.5), true},
        {__LINE__, "10 random traceIDs", UINT64_C(12078589664685934330), *Rate::from(0.5), false},
        {__LINE__, "10 random traceIDs", UINT64_C(13794769880582338323), *Rate::from(0.5), true},
        {__LINE__, "10 random traceIDs", UINT64_C(14629469446186818297), *Rate::from(0.5), false},
    }));
  // clang-format on

  CAPTURE(test_case.name);
  CAPTURE(test_case.line);
  CAPTURE(test_case.id);
  CAPTURE(test_case.probability.value());
  CAPTURE(test_case.expected_keep);

  const auto hashed_id = knuth_hash(test_case.id);
  const auto threshold = max_id_from_rate(test_case.probability);
  CAPTURE(hashed_id);
  CAPTURE(threshold);
  REQUIRE((hashed_id < threshold) == test_case.expected_keep);
}
