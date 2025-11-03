#include <datadog/endpoint_guessing.h>

#include <string>

#include "test.h"

using namespace datadog::tracing;

#define TEST_ENDPOINT(x) TEST_CASE(x, "[endpoint_guessing]")

TEST_ENDPOINT("invalid inputs and root") {
  CHECK(guess_endpoint("") == "/");
  CHECK(guess_endpoint("abc") == "/");
  CHECK(guess_endpoint("/") == "/");
  CHECK(guess_endpoint("////") == "/");
}

TEST_ENDPOINT("skips empty components and strips query") {
  CHECK(guess_endpoint("/a//b") == "/a/b");
  CHECK(guess_endpoint("/a/b?x=y") == "/a/b");
}

TEST_ENDPOINT("int and int_id replacement") {
  CHECK(guess_endpoint("/users/12") == "/users/{param:int}");
  CHECK(guess_endpoint("/v1/0-1_2.3") == "/v1/{param:int_id}");
  CHECK(guess_endpoint("/x/09") == "/x/09");  // leading zero not int
  CHECK(guess_endpoint("/1") == "/1");        // single digit not int/int_id
}

TEST_ENDPOINT("hex and hex_id replacement") {
  CHECK(guess_endpoint("/x/abcde9") == "/x/{param:hex}");
  CHECK(guess_endpoint("/x/ab_cd-9") == "/x/{param:hex_id}");
}

TEST_ENDPOINT("str replacement by special or length") {
  CHECK(guess_endpoint("/x/a%z") == "/x/{param:str}");
  std::string longseg(20, 'a');
  std::string path = std::string("/x/") + longseg;
  CHECK(guess_endpoint(path) == "/x/{param:str}");
}

TEST_ENDPOINT("other specials yield str") {
  const char specials[] = {'&', '\'', '(', ')', '*', '+', ',', ':', '=', '@'};
  for (char c : specials) {
    std::string s = "/x/a";
    s.push_back(c);
    s.push_back('b');
    CHECK(guess_endpoint(s) == "/x/{param:str}");
  }
}

TEST_ENDPOINT("max components limit") {
  const char* expected =
      "/{param:int}/{param:int}/{param:int}/{param:int}/{param:int}/"
      "{param:int}/{param:int}/{param:int}";
  CHECK(guess_endpoint("/11/22/33/44/55/66/77/88/99/12") == expected);
}

TEST_ENDPOINT("minimum length boundaries") {
  // int_id requires length ≥ 3
  CHECK(guess_endpoint("/x/0-") == "/x/0-");
  CHECK(guess_endpoint("/x/0__") == "/x/{param:int_id}");

  // hex requires length ≥ 6
  CHECK(guess_endpoint("/x/abcd9") == "/x/abcd9");

  // hex_id requires length ≥ 6
  CHECK(guess_endpoint("/x/ab_c9") == "/x/ab_c9");
  CHECK(guess_endpoint("/x/ab_cd9") == "/x/{param:hex_id}");

  // str requires length ≥ 20 (when no special characters)
  CHECK(guess_endpoint("/x/aaaaaaaaaaaaaaaaaaa") == "/x/aaaaaaaaaaaaaaaaaaa");
}
