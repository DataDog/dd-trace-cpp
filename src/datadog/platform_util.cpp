#include "platform_util.h"

#include <pthread.h>
#include <sys/types.h>
#include <sys/utsname.h>
#include <unistd.h>

#include <fstream>

#include "string_util.h"

#if defined(__x86_64__) || defined(_M_X64)
#define DD_SDK_CPU_ARCH "x86_64"
#elif defined(i386) || defined(__i386__) || defined(__i386) || defined(_M_IX86)
#define DD_SDK_CPU_ARCH "x86"
#elif defined(__aarch64__) || defined(_M_ARM64)
#define DD_SDK_CPU_ARCH "arm64"
#else
#define DD_SDK_CPU_ARCH "unknown"
#endif

#if defined(__APPLE__)
#include <sys/sysctl.h>
#define DD_SDK_OS "Darwin"
#define DD_SDK_KERNEL "Darwin"
#elif defined(__linux__) || defined(__unix__)
#define DD_SDK_OS "GNU/Linux"
#define DD_SDK_KERNEL "Linux"
#else
#define DD_SDK_OS "unknown"
#endif

namespace datadog {
namespace tracing {
namespace {

#if defined(__APPLE__)
std::string get_os_version() {
  char os_version[20] = "";
  size_t len = sizeof(os_version);

  sysctlbyname("kern.osproductversion", os_version, &len, NULL, 0);
  return os_version;
}
#elif defined(__linux__)
std::string get_os_version() {
  std::ifstream os_release_file("/etc/os-release");
  if (!os_release_file.is_open()) {
    return "";
  }

  std::string line;

  while (std::getline(os_release_file, line)) {
    size_t pos = line.find('=');
    if (pos == std::string::npos) {
      continue;
    }

    std::string key = line.substr(0, pos);
    to_lower(key);
    if (key == "version") {
      std::string value = line.substr(pos + 1);
      return value;
    }
  }

  return "";
}
#else
std::string get_os_version() { return ""; }
#endif

#if defined(__APPLE__) || defined(__linux__)

HostInfo _get_host_info() {
  HostInfo res;

  struct utsname buffer;
  if (uname(&buffer) != 0) {
    return res;
  }

  res.os = DD_SDK_OS;
  res.os_version = get_os_version();
  res.hostname = buffer.nodename;
  res.cpu_architecture = DD_SDK_CPU_ARCH;
  res.kernel_name = DD_SDK_KERNEL;
  res.kernel_version = buffer.version;
  res.kernel_release = buffer.release;

  return res;
}

#endif

}  // namespace

HostInfo get_host_info() {
  static const HostInfo host_info = _get_host_info();
  return host_info;
}

std::string get_hostname() { return get_host_info().hostname; }

int get_process_id() { return ::getpid(); }

int at_fork_in_child(void (*on_fork)()) {
  // https://pubs.opengroup.org/onlinepubs/9699919799/functions/pthread_atfork.html
  return pthread_atfork(/*before fork*/ nullptr, /*in parent*/ nullptr,
                        /*in child*/ on_fork);
}

}  // namespace tracing
}  // namespace datadog
