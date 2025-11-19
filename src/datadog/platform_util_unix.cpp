#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/statfs.h>
#include <sys/types.h>
#include <sys/utsname.h>
#include <unistd.h>

#include <cassert>
#include <cstdint>
#include <fstream>
#include <regex>
#include <string>

#include "platform_util.h"
#include "string_util.h"

#define DD_SDK_OS "GNU/Linux"
#define DD_SDK_KERNEL "Linux"

namespace fs = std::filesystem;

namespace datadog {
namespace tracing {
namespace {

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
  return fs::path(program_invocation_name);
}

std::string get_process_name() { return program_invocation_short_name; }

int at_fork_in_child(void (*on_fork)()) {
  // https://pubs.opengroup.org/onlinepubs/9699919799/functions/pthread_atfork.html
  return pthread_atfork(/*before fork*/ nullptr, /*in parent*/ nullptr,
                        /*in child*/ on_fork);
}

InMemoryFile::InMemoryFile(void* handle) : handle_(handle) {}

InMemoryFile::InMemoryFile(InMemoryFile&& rhs) {
  std::swap(rhs.handle_, handle_);
}

InMemoryFile& InMemoryFile::operator=(InMemoryFile&& rhs) {
  std::swap(handle_, rhs.handle_);
  return *this;
}

InMemoryFile::~InMemoryFile() {
  /// NOTE(@dmehala): No need to close the fd since it is automatically handled
  /// by `MFD_CLOEXEC`.
  if (handle_ == nullptr) return;
  int* data = static_cast<int*>(handle_);
  close(*data);
  delete (data);
}

bool InMemoryFile::write_then_seal(const std::string& data) {
  int fd = *static_cast<int*>(handle_);

  size_t written = write(fd, data.data(), data.size());
  if (written != data.size()) return false;

  return fcntl(fd, F_ADD_SEALS,
               F_SEAL_SHRINK | F_SEAL_GROW | F_SEAL_WRITE | F_SEAL_SEAL) == 0;
}

Expected<InMemoryFile> InMemoryFile::make(StringView name) {
  int fd = memfd_create(name.data(), MFD_CLOEXEC | MFD_ALLOW_SEALING);
  if (fd == -1) {
    std::string err_msg = "failed to create an anonymous file. errno = ";
    err_msg += std::to_string(errno);
    return Error{Error::Code::OTHER, std::move(err_msg)};
  }

  int* handle = new int;
  *handle = fd;
  return InMemoryFile(handle);
}

namespace container {
namespace {
/// Magic numbers from linux/magic.h:
/// <https://github.com/torvalds/linux/blob/ca91b9500108d4cf083a635c2e11c884d5dd20ea/include/uapi/linux/magic.h#L71>
constexpr uint64_t TMPFS_MAGIC = 0x01021994;
constexpr uint64_t CGROUP_SUPER_MAGIC = 0x27e0eb;
constexpr uint64_t CGROUP2_SUPER_MAGIC = 0x63677270;

/// Magic number from linux/proc_ns.h:
/// <https://github.com/torvalds/linux/blob/5859a2b1991101d6b978f3feb5325dad39421f29/include/linux/proc_ns.h#L41-L49>
constexpr ino_t HOST_CGROUP_NAMESPACE_INODE = 0xeffffffb;

/// Represents the cgroup version of the current process.
enum class Cgroup : char { v1, v2 };

Optional<ino_t> get_inode(std::string_view path) {
  struct stat buf;
  if (stat(path.data(), &buf) != 0) {
    return nullopt;
  }

  return buf.st_ino;
}

// Host namespace inode number are hardcoded, which allows for dectection of
// whether the binary is running in host or not. However, it does not work when
// running in a Docker in Docker environment.
bool is_running_in_host_namespace() {
  // linux procfs file that represents the cgroup namespace of the current
  // process.
  if (auto inode = get_inode("/proc/self/ns/cgroup")) {
    return *inode == HOST_CGROUP_NAMESPACE_INODE;
  }

  return false;
}

Optional<Cgroup> get_cgroup_version() {
  struct statfs buf;

  if (statfs("/sys/fs/cgroup", &buf) != 0) {
    return nullopt;
  }

  if (buf.f_type == CGROUP_SUPER_MAGIC || buf.f_type == TMPFS_MAGIC)
    return Cgroup::v1;
  else if (buf.f_type == CGROUP2_SUPER_MAGIC)
    return Cgroup::v2;

  return nullopt;
}

Optional<std::string> find_container_id_from_cgroup() {
  auto cgroup_fd = std::ifstream("/proc/self/cgroup", std::ios::in);
  if (!cgroup_fd.is_open()) return nullopt;

  return find_container_id(cgroup_fd);
}
}  // namespace

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

Optional<ContainerID> get_id() {
  auto maybe_cgroup = get_cgroup_version();
  if (!maybe_cgroup) return nullopt;

  ContainerID id;
  switch (*maybe_cgroup) {
    case Cgroup::v1: {
      if (auto maybe_id = find_container_id_from_cgroup()) {
        id.value = *maybe_id;
        id.type = ContainerID::Type::container_id;
        break;
      }
    }
      // NOTE(@dmehala): failed to find the container ID, try getting the cgroup
      // inode.
      [[fallthrough]];
    case Cgroup::v2: {
      if (!is_running_in_host_namespace()) {
        auto maybe_inode = get_inode("/sys/fs/cgroup");
        if (maybe_inode) {
          id.type = ContainerID::Type::cgroup_inode;
          id.value = std::to_string(*maybe_inode);
        }
      }
    }; break;
  }

  return id;
}

}  // namespace container

}  // namespace tracing
}  // namespace datadog
