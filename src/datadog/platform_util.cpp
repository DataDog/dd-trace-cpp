#include "platform_util.h"

#ifdef _MSC_VER
#include <processthreadsapi.h>
#include <winsock2.h>
#else
#include <pthread.h>
#include <unistd.h>
#endif

namespace datadog {
namespace tracing {
namespace {

// On Windows, `::gethostname()` requires that the winsock library is runtime
// initialized already.
//
// `void init_winsock_if_not_already()` initializes a static `class
// WinsockLibraryGuard` instance. Because it's `static`, the instance will be
// initialized at most once.
//
// `class WinsockLibraryGuard` initializes winsock in its constructor and
// cleans up winsock in its destructor.
// See
// <https://learn.microsoft.com/en-us/windows/win32/api/winsock/nf-winsock-wsastartup>.
#ifdef _MSC_VER
class WinsockLibraryGuard {
  int startup_rc_;

 public:
  WinsockLibraryGuard() {
    const WORD version_requested = MAKEWORD(2, 2);
    WSADATA info;
    startup_rc_ = WSAStartup(version_requested, &info);
  }

  ~WinsockLibraryGuard() {
    // Call cleanup, but only if `WSAStartup` was successful.
    if (startup_rc_ == 0) {
      WSACleanup();
    }
  }
};

void init_winsock_if_not_already() {
  static WinsockLibraryGuard guard;
  (void)guard;
}

#endif

}  // namespace

Optional<std::string> get_hostname() {
  init_winsock_if_not_already();
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
