// This test covers operations defined for `class TraceID` in `trace_id.h`.

#include <datadog/error.h>
#include <datadog/optional.h>
#include <datadog/trace_id.h>

#include "test.h"

using namespace datadog::tracing;

TEST_CASE("TraceID defaults to zero") {
  TraceID id;
  REQUIRE(id.low == 0);
  REQUIRE(id.high == 0);
}

TEST_CASE("TraceID parsed from hexadecimal") {
  // TODO: leading zeroes might be a corner case.
  struct TestCase {
    int line;
    std::string input;
    Optional<TraceID> expected_id;
    Optional<Error::Code> expected_error = nullopt;
  };

  // clang-format off
  const auto test_case = GENERATE(values<TestCase>({
        {__LINE__, "00001", TraceID(1)},
        {__LINE__, "0000000000000000000000000000000000000000000001", TraceID(1)},
        {__LINE__, "", nullopt, Error::INVALID_INTEGER},
        {__LINE__, "nonsense", nullopt, Error::INVALID_INTEGER},
        {__LINE__, "1000000000000000000000000000000000000000000000", nullopt, Error::OUT_OF_RANGE_INTEGER},
        {__LINE__, "deadbeefdeadbeef", TraceID{0xdeadbeefdeadbeefULL}},
        {__LINE__, "0xdeadbeefdeadbeef", nullopt, Error::INVALID_INTEGER},
        {__LINE__, "cafebabecafebabedeadbeefdeadbeef", TraceID{0xdeadbeefdeadbeefULL, 0xcafebabecafebabe}},
        {__LINE__, "caxxxxxxcafebabedeadbeefdeadbeef", nullopt, Error::INVALID_INTEGER},
        {__LINE__, "cafebabecafebabedeaxxxxxxxxdbeef", nullopt, Error::INVALID_INTEGER},
  }));
  // clang-format on

  CAPTURE(test_case.line);
  CAPTURE(test_case.input);
  const auto result = TraceID::parse_hex(test_case.input);
  if (test_case.expected_error) {
    REQUIRE_FALSE(result);
    REQUIRE(result.error().code == *test_case.expected_error);
  } else {
    REQUIRE(result);
    REQUIRE(*result == *test_case.expected_id);
  }
}
