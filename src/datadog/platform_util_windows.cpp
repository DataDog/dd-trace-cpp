// clang-format off
#include <windows.h>
// clang-format on
#include <processthreadsapi.h>
#include <winsock.h>

#include <cassert>
#include <cstdint>
#include <fstream>
#include <regex>

#include "platform_util.h"

namespace datadog {
namespace tracing {
namespace {

std::tuple<std::string, std::string> get_windows_info() {
  // NOTE(@dmehala): Retrieving the Windows version has been complicated since
  // Windows 8.1. The `GetVersion` function and its variants depend on the
  // application manifest, which is the lowest version supported by the
  // application. Use `RtlGetVersion` to obtain the accurate OS version
  // regardless of the manifest.
  using RtlGetVersion = auto (*)(LPOSVERSIONINFOEXW)->NTSTATUS;

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

}  // namespace

HostInfo get_host_info() {
  static const HostInfo host_info = _get_host_info();
  return host_info;
}

std::string get_hostname() { return get_host_info().hostname; }

int get_process_id() { return GetCurrentProcessId(); }

std::string get_process_name() {
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
  return path;
}

int at_fork_in_child(void (*on_fork)()) {
  // Windows does not have `fork`, and so this is not relevant there.
  (void)on_fork;
  return 0;
}

InMemoryFile::InMemoryFile(void* handle) : handle_(handle) {}

InMemoryFile::InMemoryFile(InMemoryFile&& rhs) {
  std::swap(rhs.handle_, handle_);
}

InMemoryFile& InMemoryFile::operator=(InMemoryFile&& rhs) {
  std::swap(handle_, rhs.handle_);
  return *this;
}

InMemoryFile::~InMemoryFile() {}
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
