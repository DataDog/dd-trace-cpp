#include "id_generator.h"

#include <cstddef>
#include <limits>
#include <random>

#ifndef _MSC_VER
#include <pthread.h>
#endif

namespace datadog {
namespace tracing {
namespace {

#ifndef _MSC_VER
extern "C" void on_fork();
#endif

class DefaultIDGenerator {
  std::mt19937_64 generator_;
  // The distribution used is for a _signed_ integer, and the default minimum
  // value is zero.
  // This means that we generate IDs with non-negative values that will always
  // fit into an `int64_t`, which is a polite thing to do when you work with
  // people who write Java.
  std::uniform_int_distribution<std::int64_t> distribution_;

 public:
  DefaultIDGenerator() {
    seed_with_random();
// If a process links to this library and then calls `fork`, the `generator_` in
// the parent and child processes will produce the exact same sequence of
// values, which is bad.
// A subsequent call to `exec` would remedy this, but nginx in particular does
// not call `exec` after forking its worker processes.
// So, we use `pthread_atfork` to re-seed `generator_` in the child process
// after `fork`.
// Windows does not have `fork`, and so this is not relevant there.
#ifndef _MSC_VER
    // https://pubs.opengroup.org/onlinepubs/9699919799/functions/pthread_atfork.html
    (void)pthread_atfork(/*before fork*/ nullptr, /*in parent*/ nullptr,
                         /*in child*/ &on_fork);
#endif
  }

  std::uint64_t operator()() { return distribution_(generator_); }

  void seed_with_random() { generator_.seed(std::random_device{}()); }
};

thread_local DefaultIDGenerator thread_local_generator;

#ifndef _MSC_VER
void on_fork() { thread_local_generator.seed_with_random(); }
#endif

}  // namespace

const IDGenerator default_id_generator = []() {
  return thread_local_generator();
};

}  // namespace tracing
}  // namespace datadog
