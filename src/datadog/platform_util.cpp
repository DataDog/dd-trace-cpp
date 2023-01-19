#include "platform_util.h"

#ifdef _MSC_VER
#include <processthreadsapi.h>
#include <winsock.h>
#else
#include <pthread.h>
#include <unistd.h>
#endif

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
#ifdef _MSC_VER
  return GetCurrentProcessId();
#else
  return ::getpid();
#endif
}

int at_fork_in_child(void (*on_fork)()) {
// Windows does not have `fork`, and so this is not relevant there.
#ifdef _MSC_VER
  return 0;
#else
  // https://pubs.opengroup.org/onlinepubs/9699919799/functions/pthread_atfork.html
  return pthread_atfork(/*before fork*/ nullptr, /*in parent*/ nullptr,
                        /*in child*/ on_fork);
#endif
}

}  // namespace tracing
}  // namespace datadog
