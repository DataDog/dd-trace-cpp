#include "platform_util.h"

// clang-format off
#if defined(DD_TRACE_PLATFORM_WINDOWS)
#  include <windows.h>
#  include <processthreadsapi.h>
#  include <winsock.h>
#else
#  include <pthread.h>
#  include <unistd.h>
#endif
// clang-format on

namespace datadog {
namespace tracing {

Optional<std::string> get_hostname() {
  char buffer[256];
  if (::gethostname(buffer, sizeof buffer)) {
    return nullopt;
  }
  return buffer;
}

int get_process_id() {
#if defined(DD_TRACE_PLATFORM_WINDOWS)
  return GetCurrentProcessId();
#else
  return ::getpid();
#endif
}

int at_fork_in_child(void (*on_fork)()) {
#if defined(DD_TRACE_PLATFORM_WINDOWS)
  // Windows does not have `fork`, and so this is not relevant there.
  return 0;
#else
  // https://pubs.opengroup.org/onlinepubs/9699919799/functions/pthread_atfork.html
  return pthread_atfork(/*before fork*/ nullptr, /*in parent*/ nullptr,
                        /*in child*/ on_fork);
#endif
}

}  // namespace tracing
}  // namespace datadog
