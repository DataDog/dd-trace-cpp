#include <sstream>

#include "platform_util.h"
#include "test.h"

using namespace datadog::tracing;

#define PLATFORM_UTIL_TEST(x) TEST_CASE(x, "[platform_util]")

PLATFORM_UTIL_TEST("find docker container ID") {
  struct TestCase {
    size_t line;
    std::string_view name;
    std::string input;
    Optional<std::string> expected_container_id;
  };

  auto test_case = GENERATE(values<TestCase>({
      {__LINE__, "empty inputs", "", nullopt},
      {__LINE__, "no docker container ID", "coucou", nullopt},
      {__LINE__, "one line with docker container ID",
       "0::/system.slice/docker-cde7c2bab394630a42d73dc610b9c57415dced996106665d427f6d0566594411.scope",
       "cde7c2bab394630a42d73dc610b9c57415dced996106665d427f6d0566594411"},
      {__LINE__, "multiline wihtout docker container ID", R"(
0::/
10:memory:/user.slice/user-0.slice/session-14.scope
9:hugetlb:/
8:cpuset:/
7:pids:/user.slice/user-0.slice/session-14.scope
6:freezer:/
5:net_cls,net_prio:/
4:perf_event:/
3:cpu,cpuacct:/user.slice/user-0.slice/session-14.scope
2:devices:/user.slice/user-0.slice/session-14.scope
1:name=systemd:/user.slice/user-0.slice/session-14.scope
)",
       nullopt},
      {__LINE__, "multiline with docker container ID", R"(
11:blkio:/user.slice/user-0.slice/session-14.scope
10:memory:/user.slice/user-0.slice/session-14.scope
9:hugetlb:/
8:cpuset:/
7:pids:/user.slice/user-0.slice/session-14.scope
3:cpu:/system.slice/docker-cde7c2bab394630a42d73dc610b9c57415dced996106665d427f6d0566594411.scope
6:freezer:/
5:net_cls,net_prio:/
4:perf_event:/
3:cpu,cpuacct:/user.slice/user-0.slice/session-14.scope
2:devices:/user.slice/user-0.slice/session-14.scope
1:name=systemd:/user.slice/user-0.slice/session-14.scope
    )",
       "cde7c2bab394630a42d73dc610b9c57415dced996106665d427f6d0566594411"},
  }));

  CAPTURE(test_case.name);

  std::istringstream is(test_case.input);

  auto maybe_container_id = container::find_docker_container_id(is);
  if (test_case.expected_container_id.has_value()) {
    REQUIRE(maybe_container_id.has_value());
    CHECK(*maybe_container_id == *test_case.expected_container_id);
  } else {
    CHECK(!maybe_container_id.has_value());
  }
}
