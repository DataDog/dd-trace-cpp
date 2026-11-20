#include <array>
#include <set>
#include <thread>
#include <vector>

#if defined(_AIX) || defined(__linux__) || defined(__APPLE__)
#include <sys/wait.h>
#include <unistd.h>
#endif

#include "random.h"
#include "test.h"

using namespace datadog::tracing;

TEST_CASE("random generators survive thread exit", "[random]") {
  std::array<std::array<std::uint64_t, 16>, 16> samples{};
  std::vector<std::thread> threads;
  for (auto& sample : samples) {
    threads.emplace_back([&sample] {
      for (auto& value : sample) value = random_uint64();
    });
  }
  for (auto& thread : threads) thread.join();
  std::set<std::uint64_t> distinct;
  for (const auto& sample : samples) {
    distinct.insert(sample.begin(), sample.end());
  }
  CHECK(distinct.size() == samples.size() * samples.front().size());
}

#if defined(_AIX) || defined(__linux__) || defined(__APPLE__)
TEST_CASE("random generator is reseeded after fork", "[random]") {
  (void)random_uint64();
  int descriptors[2];
  REQUIRE(pipe(descriptors) == 0);
  const pid_t child = fork();
  REQUIRE(child >= 0);
  if (child == 0) {
    close(descriptors[0]);
    alarm(10);
    const auto value = random_uint64();
    const auto written = write(descriptors[1], &value, sizeof(value));
    _exit(written == sizeof(value) ? 0 : 1);
  }
  close(descriptors[1]);
  const auto parent_value = random_uint64();
  std::uint64_t child_value{};
  const auto received = read(descriptors[0], &child_value, sizeof(child_value));
  close(descriptors[0]);
  int status{};
  REQUIRE(waitpid(child, &status, 0) == child);
  REQUIRE(WIFEXITED(status));
  REQUIRE(WEXITSTATUS(status) == 0);
  REQUIRE(received == sizeof(child_value));
  CHECK(child_value != parent_value);
}
#endif
