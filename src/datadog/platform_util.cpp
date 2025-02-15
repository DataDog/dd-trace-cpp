#include "platform_util.h"

// clang-format off
#if defined(__x86_64__) || defined(_M_X64)
#  define DD_SDK_CPU_ARCH "x86_64"
#elif defined(i386) || defined(__i386__) || defined(__i386) || defined(_M_IX86)
#  define DD_SDK_CPU_ARCH "x86"
#elif defined(__aarch64__) || defined(_M_ARM64)
#  define DD_SDK_CPU_ARCH "arm64"
#else
#  define DD_SDK_CPU_ARCH "unknown"
#endif

#if defined(__APPLE__) || defined(__linux__) || defined(__unix__)
#  include <pthread.h>
#  include <sys/types.h>
#  include <sys/utsname.h>
#  include <unistd.h>
#  if defined(__APPLE__)
#    include <sys/sysctl.h>
#    define DD_SDK_OS "Darwin"
#    define DD_SDK_KERNEL "Darwin"
#  elif defined(__linux__) || defined(__unix__)
#    define DD_SDK_OS "GNU/Linux"
#    define DD_SDK_KERNEL "Linux"
#    include "string_util.h"
#    include <fstream>
#    include <sys/types.h>
#    include <sys/mman.h>
#    include <fcntl.h>
#    include <errno.h>
#  endif
#elif defined(_MSC_VER)
#  include <windows.h>
#  include <processthreadsapi.h>
#  include <winsock.h>
#endif
// clang-format on

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
#endif

#if defined(__APPLE__) || defined(__linux__) || defined(__unix__)
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
#elif defined(_MSC_VER)
std::tuple<std::string, std::string> get_windows_info() {
  // NOTE(@dmehala): Retrieving the Windows version has been complicated since
  // Windows 8.1. The `GetVersion` function and its variants depend on the
  // application manifest, which is the lowest version supported by the
  // application. Use `RtlGetVersion` to obtain the accurate OS version
  // regardless of the manifest.
  using RtlGetVersion = auto(*)(LPOSVERSIONINFOEXW)->NTSTATUS;

  RtlGetVersion func =
      (RtlGetVersion)GetProcAddress(GetModuleHandleA("ntdll"), "RtlGetVersion");

  if (func) {
    OSVERSIONINFOEXW os_info;
    ZeroMemory(&os_info, sizeof(OSVERSIONINFO));
    os_info.dwOSVersionInfoSize = sizeof(os_info);

    if (func(&os_info) == 0) {
      switch (os_info.dwMajorVersion) {
        case 5: {
          switch (os_info.dwMinorVersion) {
            case 0:
              return {"Windows 2000", "NT 5.0"};
            case 1:
              return {"Windows XP", "NT 5.1"};
            case 2:
              return {"Windows XP", "NT 5.2"};
            default:
              return {"Windows XP", "NT 5.x"};
          }
        }; break;
        case 6: {
          switch (os_info.dwMinorVersion) {
            case 0:
              return {"Windows Vista", "NT 6.0"};
            case 1:
              return {"Windows 7", "NT 6.1"};
            case 2:
              return {"Windows 8", "NT 6.2"};
            case 3:
              return {"Windows 8.1", "NT 6.3"};
            default:
              return {"Windows 8.1", "NT 6.x"};
          }
        }; break;
        case 10: {
          if (os_info.dwBuildNumber >= 10240 && os_info.dwBuildNumber < 22000) {
            return {"Windows 10", "NT 10.0"};
          } else if (os_info.dwBuildNumber >= 22000) {
            return {"Windows 11", "21H2"};
          }
        }; break;
      }
    }
  }

  return {"", ""};
}

HostInfo _get_host_info() {
  HostInfo host;
  host.cpu_architecture = DD_SDK_CPU_ARCH;

  auto [os, os_version] = get_windows_info();
  host.os = std::move(os);
  host.os_version = std::move(os_version);

  char buffer[256];
  if (0 == gethostname(buffer, sizeof(buffer))) {
    host.hostname = buffer;
  }

  return host;
}
#else
HostInfo _get_host_info() {
  HostInfo res;
  return res;
}
#endif

}  // namespace

HostInfo get_host_info() {
  static const HostInfo host_info = _get_host_info();
  return host_info;
}

std::string get_hostname() { return get_host_info().hostname; }

int get_process_id() {
#if defined(_MSC_VER)
  return GetCurrentProcessId();
#else
  return ::getpid();
#endif
}

std::string get_process_name() {
#if defined(__APPLE__) || defined(__FreeBSD__)
  const char* process_name = getprogname();
  return (process_name != nullptr) ? process_name : "unknown-service";
#elif defined(__linux__) || defined(__unix__)
  return program_invocation_short_name;
#elif defined(_MSC_VER)
  TCHAR exe_name[MAX_PATH];
  if (GetModuleFileName(NULL, exe_name, MAX_PATH) <= 0) {
    return "unknown-service";
  }
#ifdef UNICODE
  std::wstring wStr(exe_name);
  std::string path = std::string(wStr.begin(), wStr.end());
#else
  std::string path = std::string(exe_name);
#endif
  return path.substr(path.find_last_of("/\\") + 1);
#else
  return "unknown-service";
#endif
}

int at_fork_in_child(void (*on_fork)()) {
#if defined(_MSC_VER)
  // Windows does not have `fork`, and so this is not relevant there.
  (void)on_fork;
  return 0;
#else
  // https://pubs.opengroup.org/onlinepubs/9699919799/functions/pthread_atfork.html
  return pthread_atfork(/*before fork*/ nullptr, /*in parent*/ nullptr,
                        /*in child*/ on_fork);
#endif
}

InMemoryFile::InMemoryFile(void* handle) : handle_(handle) {}

InMemoryFile::InMemoryFile(InMemoryFile&& rhs) {
  std::swap(rhs.handle_, handle_);
}

InMemoryFile& InMemoryFile::operator=(InMemoryFile&& rhs) {
  std::swap(handle_, rhs.handle_);
  return *this;
}

#if defined(__linux__) || defined(__unix__)

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

#else
InMemoryFile::~InMemoryFile() {}
bool InMemoryFile::write_then_seal(const std::string&) { return false; }
Expected<InMemoryFile> InMemoryFile::make(StringView) {
  return Error{Error::Code::NOT_IMPLEMENTED, "In-memory file not implemented"};
}
#endif

}  // namespace tracing
}  // namespace datadog
