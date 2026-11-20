#include <pthread.h>
#include <sys/procfs.h>
#include <sys/utsname.h>
#include <unistd.h>

#include <cstring>
#include <fstream>
#include <utility>

#include "platform_util.h"

namespace datadog {
namespace tracing {

HostInfo get_host_info() {
  struct utsname system_info{};
  HostInfo result;
  if (uname(&system_info) != 0) return result;
  result.os = "AIX";
  result.os_version =
      std::string(system_info.version) + "." + system_info.release;
  result.hostname = system_info.nodename;
  result.cpu_architecture = DD_SDK_CPU_ARCH;
  result.kernel_name = system_info.sysname;
  result.kernel_version = system_info.version;
  result.kernel_release = system_info.release;
  return result;
}

std::string get_hostname() { return get_host_info().hostname; }
int get_process_id() { return ::getpid(); }
std::string get_process_name() {
  std::ifstream process_info("/proc/" + std::to_string(getpid()) + "/psinfo",
                             std::ios::binary);
  psinfo_t info{};
  if (!process_info.read(reinterpret_cast<char*>(&info), sizeof(info))) {
    return "unknown-service";
  }
  return std::string(info.pr_fname,
                     strnlen(info.pr_fname, sizeof(info.pr_fname)));
}
Optional<std::filesystem::path> get_process_path() { return nullopt; }

int at_fork_in_child(void (*on_fork)()) {
  return pthread_atfork(nullptr, nullptr, on_fork);
}

InMemoryFile::InMemoryFile(void* handle) : handle_(handle) {}
InMemoryFile::~InMemoryFile() {}
InMemoryFile::InMemoryFile(InMemoryFile&& other) {
  std::swap(handle_, other.handle_);
}
InMemoryFile& InMemoryFile::operator=(InMemoryFile&& other) {
  std::swap(handle_, other.handle_);
  return *this;
}
bool InMemoryFile::write_then_seal(const std::string&) { return false; }
Expected<InMemoryFile> InMemoryFile::make(StringView) {
  return Error{Error::Code::NOT_IMPLEMENTED,
               "AIX has no sealed in-memory files"};
}

namespace container {
Optional<std::string> find_container_id(std::istream&) { return nullopt; }
Optional<ContainerID> get_id() { return nullopt; }
}  // namespace container

}  // namespace tracing
}  // namespace datadog
