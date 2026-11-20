#include <datadog/base64.h>

#include "catch.hpp"
#include "test.h"

#define BASE64_TEST(x) TEST_CASE(x, "[base64]")

using namespace datadog::tracing;

BASE64_TEST("empty input") { CHECK(base64_decode("") == ""); }

BASE64_TEST("invalid inputs") {
  SECTION("invalid characters") {
    CHECK(base64_decode("InvalidData@") == "");
    CHECK(base64_decode("In@#*!^validData") == "");
  }

  SECTION("single character without padding") {
    CHECK(base64_decode("V") == "");
  }
}

BASE64_TEST("unpadded input") {
  CHECK(base64_decode("VGVzdGluZyBtdWx0aXBsZSBvZiA0IHBhZGRpbmcu") ==
        "Testing multiple of 4 padding.");
}

BASE64_TEST("padding") {
  CHECK(base64_decode("bGlnaHQgdw==") == "light w");
  CHECK(base64_decode("bGlnaHQgd28=") == "light wo");
  CHECK(base64_decode("bGlnaHQgd29y") == "light wor");
}

BASE64_TEST("encode") {
  CHECK(base64_encode("") == "");
  CHECK(base64_encode("f") == "Zg==");
  CHECK(base64_encode("fo") == "Zm8=");
  CHECK(base64_encode("foo") == "Zm9v");
  CHECK(base64_encode("foob") == "Zm9vYg==");
  CHECK(base64_encode("fooba") == "Zm9vYmE=");
  CHECK(base64_encode("foobar") == "Zm9vYmFy");
}

BASE64_TEST("encode bytes above 0x7f") {
  CHECK(base64_encode("\xff\xfe") == "//4=");
  CHECK(base64_encode(std::string("\x00\x80\xfb", 3)) == "AID7");
}
