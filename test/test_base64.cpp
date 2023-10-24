#include <datadog/base64.h>

#include "catch.hpp"
#include "test.h"

#define BASE64_TEST(x) TEST_CASE(x, "[base64]")

namespace base64 = datadog::tracing::base64;

BASE64_TEST("empty input") { CHECK(base64::decode("") == ""); }

BASE64_TEST("invalid inputs") {
  SECTION("invalid characters") {
    CHECK(base64::decode("InvalidData@") == "");
    CHECK(base64::decode("In@#*!^validData") == "");
  }

  SECTION("single character without padding") {
    CHECK(base64::decode("V") == "");
  }
}

BASE64_TEST("unpadded input") {
  CHECK(base64::decode("VGVzdGluZyBtdWx0aXBsZSBvZiA0IHBhZGRpbmcu") ==
        "Testing multiple of 4 padding.");
}

BASE64_TEST("padding") {
  CHECK(base64::decode("bGlnaHQgdw==") == "light w");
  CHECK(base64::decode("bGlnaHQgd28=") == "light wo");
  CHECK(base64::decode("bGlnaHQgd29y") == "light wor");
}
