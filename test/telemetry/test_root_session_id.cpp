#include <datadog/runtime_id.h>

#include "../test.h"
#include "root_session_id.h"

#ifndef _WIN32
#include <sys/wait.h>
#include <unistd.h>
#endif

using namespace datadog::tracing;

#define ROOT_SESSION_ID_TEST(x) \
  TEST_CASE(x, "[telemetry],[telemetry.root_session_id]")

ROOT_SESSION_ID_TEST("get_or_init returns the first runtime ID") {
  auto first_rid = RuntimeID::generate();
  auto second_rid = RuntimeID::generate();

  const auto& result1 = root_session_id::get_or_init(first_rid.string());
  const auto& result2 = root_session_id::get_or_init(second_rid.string());

  CHECK(result1 == first_rid.string());
  CHECK(result2 == first_rid.string());
  CHECK(result1 == result2);
}

#ifndef _WIN32
ROOT_SESSION_ID_TEST("child process inherits root session ID after fork") {
  // get_or_init may already be set from a previous test; capture whatever the
  // current singleton value is.
  auto parent_rid = RuntimeID::generate();
  const auto& root_id = root_session_id::get_or_init(parent_rid.string());

  int pipefd[2];
  REQUIRE(pipe(pipefd) == 0);

  pid_t pid = fork();
  REQUIRE(pid >= 0);

  if (pid == 0) {
    // Child: call get_or_init with a different ID, write result to pipe.
    close(pipefd[0]);
    auto child_rid = RuntimeID::generate();
    const auto& result = root_session_id::get_or_init(child_rid.string());
    auto written =
        write(pipefd[1], result.data(), result.size());
    close(pipefd[1]);
    _exit(written == static_cast<ssize_t>(result.size()) ? 0 : 1);
  }

  // Parent: read child's result from pipe.
  close(pipefd[1]);
  char buf[128] = {};
  auto n = read(pipefd[0], buf, sizeof(buf) - 1);
  close(pipefd[0]);

  int status = 0;
  waitpid(pid, &status, 0);
  REQUIRE(WIFEXITED(status));
  REQUIRE(WEXITSTATUS(status) == 0);
  REQUIRE(n > 0);

  std::string child_result(buf, n);
  CHECK(child_result == root_id);
}
#endif
