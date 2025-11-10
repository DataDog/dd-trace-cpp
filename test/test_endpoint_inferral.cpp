#include <datadog/endpoint_inferral.h>

#include <string>

#include "test.h"

using namespace datadog::tracing;

#define TEST_ENDPOINT(x) TEST_CASE(x, "[endpoint_inferral]")

TEST_ENDPOINT("invalid inputs and root") {
  CHECK(infer_endpoint("") == "/");
  CHECK(infer_endpoint("abc") == "/");
  CHECK(infer_endpoint("/") == "/");
  CHECK(infer_endpoint("////") == "/");
}

TEST_ENDPOINT("skips empty components") {
  CHECK(infer_endpoint("/a//b") == "/a/b");
  CHECK(infer_endpoint("/a/b/") == "/a/b/");
}

TEST_ENDPOINT("int and int_id replacement") {
  CHECK(infer_endpoint("/users/12") == "/users/{param:int}");
  CHECK(infer_endpoint("/v1/0-1_2.3") == "/v1/{param:int_id}");
  CHECK(infer_endpoint("/x/09") == "/x/09");  // leading zero not int
  CHECK(infer_endpoint("/1") == "/1");        // single digit not int/int_id
}

TEST_ENDPOINT("hex and hex_id replacement") {
  CHECK(infer_endpoint("/x/abcde9") == "/x/{param:hex}");
  CHECK(infer_endpoint("/x/ab_cd-9") == "/x/{param:hex_id}");
}

TEST_ENDPOINT("str replacement by special or length") {
  CHECK(infer_endpoint("/x/a%z") == "/x/{param:str}");
  std::string longseg(20, 'a');
  std::string path = std::string("/x/") + longseg;
  CHECK(infer_endpoint(path) == "/x/{param:str}");
}

TEST_ENDPOINT("other specials yield str") {
  const char specials[] = {'&', '\'', '(', ')', '*', '+', ',', ':', '=', '@'};
  for (char c : specials) {
    std::string s = "/x/a";
    s.push_back(c);
    s.push_back('b');
    CHECK(infer_endpoint(s) == "/x/{param:str}");
  }
}

TEST_ENDPOINT("max components limit") {
  const char* expected =
      "/{param:int}/{param:int}/{param:int}/{param:int}/{param:int}/"
      "{param:int}/{param:int}/{param:int}/";
  CHECK(infer_endpoint("/11/22/33/44/55/66/77/88/99/12") == expected);
}

TEST_ENDPOINT("minimum length boundaries") {
  // int_id requires length ≥ 3
  CHECK(infer_endpoint("/x/0-") == "/x/0-");
  CHECK(infer_endpoint("/x/0__") == "/x/{param:int_id}");

  // hex requires length ≥ 6
  CHECK(infer_endpoint("/x/abcd9") == "/x/abcd9");

  // hex_id requires length ≥ 6
  CHECK(infer_endpoint("/x/ab_c9") == "/x/ab_c9");
  CHECK(infer_endpoint("/x/ab_cd9") == "/x/{param:hex_id}");

  // str requires length ≥ 20 (when no special characters)
  CHECK(infer_endpoint("/x/aaaaaaaaaaaaaaaaaaa") == "/x/aaaaaaaaaaaaaaaaaaa");
}
