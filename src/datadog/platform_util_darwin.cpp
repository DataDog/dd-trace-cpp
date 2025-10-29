#include <libproc.h>
#include <pthread.h>
#include <sys/sysctl.h>
#include <sys/types.h>
#include <sys/utsname.h>
#include <unistd.h>

#include <cassert>
#include <cstdint>
#include <fstream>
#include <regex>

#include "platform_util.h"

#define DD_SDK_OS "Darwin"
#define DD_SDK_KERNEL "Darwin"

namespace fs = std::filesystem;

namespace datadog {
namespace tracing {
namespace {

std::string get_os_version() {
  char os_version[20] = "";
  size_t len = sizeof(os_version);

  sysctlbyname("kern.osproductversion", os_version, &len, NULL, 0);
  return os_version;
}

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

}  // namespace

HostInfo get_host_info() {
  static const HostInfo host_info = _get_host_info();
  return host_info;
}

std::string get_hostname() { return get_host_info().hostname; }

int get_process_id() { return ::getpid(); }

Optional<std::filesystem::path> get_process_path() {
  char pathbuf[PROC_PIDPATHINFO_MAXSIZE];
  if (!proc_pidpath(::getpid(), pathbuf, sizeof(pathbuf))) {
    return nullopt;
  }

  return fs::path(pathbuf);
}

std::string get_process_name() {
  const char* process_name = getprogname();
  return (process_name != nullptr) ? process_name : "unknown-service";
}

int at_fork_in_child(void (*on_fork)()) {
  // https://pubs.opengroup.org/onlinepubs/9699919799/functions/pthread_atfork.html
  return pthread_atfork(/*before fork*/ nullptr, /*in parent*/ nullptr,
                        /*in child*/ on_fork);
}

InMemoryFile::InMemoryFile(void* handle) : handle_(handle) {}

InMemoryFile::~InMemoryFile() {}

InMemoryFile::InMemoryFile(InMemoryFile&& rhs) {
  std::swap(rhs.handle_, handle_);
}

InMemoryFile& InMemoryFile::operator=(InMemoryFile&& rhs) {
  std::swap(handle_, rhs.handle_);
  return *this;
}

bool InMemoryFile::write_then_seal(const std::string&) { return false; }
Expected<InMemoryFile> InMemoryFile::make(StringView) {
  return Error{Error::Code::NOT_IMPLEMENTED, "In-memory file not implemented"};
}

namespace container {

Optional<std::string> find_container_id(std::istream& source) {
  std::string line;

  // Look for Docker container IDs in the basic format: `docker-<uuid>.scope`.
  constexpr std::string_view docker_str = "docker-";

  while (std::getline(source, line)) {
    // Example:
    // `0::/system.slice/docker-abcdef0123456789abcdef0123456789.scope`
    if (auto beg = line.find(docker_str); beg != std::string::npos) {
      beg += docker_str.size();
      auto end = line.find(".scope", beg);
      if (end == std::string::npos || end - beg <= 0) {
        continue;
      }

      auto container_id = line.substr(beg, end - beg);
      return container_id;
    }
  }

  // Reset the stream to the beginning.
  source.clear();
  source.seekg(0);

  // Perform a second pass using a regular expression for matching container IDs
  // in a Fargate environment. This two-step approach is used because STL
  // `regex` is relatively slow, so we avoid using it unless necessary.
  static const std::string uuid_regex_str =
      "[0-9a-f]{8}[-_][0-9a-f]{4}[-_][0-9a-f]{4}[-_][0-9a-f]{4}[-_][0-9a-f]{12}"
      "|(?:[0-9a-f]{8}(?:-[0-9a-f]{4}){4}$)";
  static const std::string container_regex_str = "[0-9a-f]{64}";
  static const std::string task_regex_str = "[0-9a-f]{32}-\\d+";
  static const std::regex path_reg("(?:.+)?(" + uuid_regex_str + "|" +
                                   container_regex_str + "|" + task_regex_str +
                                   ")(?:\\.scope)?$");

  while (std::getline(source, line)) {
    // Example:
    // `0::/system.slice/docker-abcdef0123456789abcdef0123456789.scope`
    std::smatch match;
    if (std::regex_match(line, match, path_reg) && match.size() == 2) {
      assert(match.ready());
      assert(match.size() == 2);

      return match.str(1);
    }
  }

  return nullopt;
}

Optional<ContainerID> get_id() { return nullopt; }

}  // namespace container

}  // namespace tracing
}  // namespace datadog
