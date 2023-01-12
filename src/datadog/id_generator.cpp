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

class Uint64Generator {
  std::mt19937_64 generator_;
  // The distribution used is for a _signed_ integer, and the default minimum
  // value is zero.
  // This means that we generate IDs with non-negative values that will always
  // fit into an `int64_t`, which is a polite thing to do when you work with
  // people who write Java.
  std::uniform_int_distribution<std::int64_t> distribution_;

 public:
  Uint64Generator() {
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

thread_local Uint64Generator thread_local_generator;

#ifndef _MSC_VER
void on_fork() { thread_local_generator.seed_with_random(); }
#endif

class DefaultIDGenerator : public IDGenerator {
  const bool trace_id_128_bit_;

 public:
  explicit DefaultIDGenerator(bool trace_id_128_bit)
      : trace_id_128_bit_(trace_id_128_bit) {}

  TraceID trace_id() const override {
    TraceID result;
    result.low = thread_local_generator();
    if (trace_id_128_bit_) {
      result.high = thread_local_generator();
    }
    return result;
  }

  std::uint64_t span_id() const override { return thread_local_generator(); }
};

}  // namespace

std::shared_ptr<const IDGenerator> default_id_generator(bool trace_id_128_bit) {
  return std::make_shared<DefaultIDGenerator>(trace_id_128_bit);
}

}  // namespace tracing
}  // namespace datadog
