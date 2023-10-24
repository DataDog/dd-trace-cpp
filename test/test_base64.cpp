#include <datadog/base64.h>

#include "test.h"

namespace base64 = datadog::tracing::base64;

TEST_CASE("empty", "[base64]") { REQUIRE(base64::decode("") == ""); }

// TEST_CASE("edge cases", "[base64]") {
//   REQUIRE(base64::from_base64("w==") == "w");
//   REQUIRE(base64::from_base64("28=") == "o");
//   REQUIRE(base64::from_base64("9y") == "r");
// }

TEST_CASE("bad input", "[base64]") { REQUIRE(base64::decode("") == ""); }

TEST_CASE("padding", "[base64]") {
  REQUIRE(base64::decode("bGlnaHQgdw==") == "light w");
  REQUIRE(base64::decode("bGlnaHQgd28=") == "light wo");
  REQUIRE(base64::decode("bGlnaHQgd29y") == "light wor");
}
