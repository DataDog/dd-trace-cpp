#include <datadog/stable_config_loader.h>

#include <cerrno>
#include <climits>
#include <cstdint>
#include <cstring>
#include <cwchar>
#include <filesystem>
#include <fstream>
#include <string>

#if defined(__APPLE__)
#include <crt_externs.h>
#elif defined(_WIN32)
#include <shellapi.h>
#include <windows.h>
#endif

#include "stable_config_loader_internal.h"

#if !defined(_WIN32) && !defined(__APPLE__)
extern char** environ;
#endif

namespace datadog::tracing {
namespace {

constexpr std::size_t max_config_file_size = 100 * 1024 * 1024;

Expected<std::string> read_config_file(const std::string& path,
                                       const char* source) {
  const std::string prefix =
      "Unable to load stable configuration: failed to read " +
      std::string{source} + " config file: ";
#if defined(_WIN32)
  if (path.size() > INT_MAX)
    return Error{Error::OTHER, prefix + "path is too long"};
  const int length =
      MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, path.data(),
                          static_cast<int>(path.size()), nullptr, 0);
  if (length == 0) return Error{Error::OTHER, prefix + "invalid UTF-8 path"};
  std::wstring wide_path(static_cast<std::size_t>(length), L'\0');
  MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, path.data(),
                      static_cast<int>(path.size()), wide_path.data(), length);
  const std::filesystem::path native_path{wide_path};
#else
  const std::filesystem::path native_path{path};
#endif
  std::error_code error;
  const std::uintmax_t size = std::filesystem::file_size(native_path, error);
  if (error == std::errc::no_such_file_or_directory) return std::string{};
  if (error) {
    return Error{Error::OTHER, prefix + error.message()};
  }
  if (size > max_config_file_size) return std::string{};
  std::ifstream file(native_path, std::ios::binary);
  if (!file) {
    if (errno == ENOENT) return std::string{};
    return Error{Error::OTHER, prefix + std::strerror(errno)};
  }
  std::string content;
  content.reserve(static_cast<std::size_t>(size));
  char buffer[8192];
  while (file.read(buffer, sizeof(buffer)) || file.gcount() != 0) {
    content.append(buffer, static_cast<std::size_t>(file.gcount()));
    if (content.size() > max_config_file_size) return std::string{};
  }
  if (file.bad()) {
    return Error{Error::OTHER, prefix + "read failed"};
  }
  return content;
}

std::string local_default_path() {
#if defined(_WIN32)
  return "C:\\ProgramData\\Datadog\\application_monitoring.yaml";
#elif defined(__APPLE__)
  return "/opt/datadog-agent/etc/application_monitoring.yaml";
#else
  return "/etc/datadog-agent/application_monitoring.yaml";
#endif
}

std::string fleet_default_path() {
#if defined(_WIN32)
  return "C:\\ProgramData\\Datadog\\managed\\datadog-"
         "agent\\stable\\application_monitoring.yaml";
#elif defined(__APPLE__)
  return "/opt/datadog-agent/etc/stable/application_monitoring.yaml";
#else
  return "/etc/datadog-agent/managed/datadog-agent/stable/"
         "application_monitoring.yaml";
#endif
}

#if defined(_WIN32)
std::string utf8(const wchar_t* value, int length) {
  const int size = WideCharToMultiByte(CP_UTF8, 0, value, length, nullptr, 0,
                                       nullptr, nullptr);
  std::string result(static_cast<std::size_t>(size), '\0');
  if (size != 0) {
    WideCharToMultiByte(CP_UTF8, 0, value, length, result.data(), size, nullptr,
                        nullptr);
  }
  return result;
}
#endif

stable_config_internal::ProcessInfo current_process(StringView language) {
  stable_config_internal::ProcessInfo result;
  if (!language.empty())
    result.language.assign(language.data(), language.size());
#if defined(_WIN32)
  int count = 0;
  wchar_t** args = CommandLineToArgvW(GetCommandLineW(), &count);
  if (args) {
    for (int index = 0; index < count; ++index) {
      result.args.push_back(utf8(args[index], -1));
      if (!result.args.back().empty()) result.args.back().pop_back();
    }
    LocalFree(args);
  }
  wchar_t* environment = GetEnvironmentStringsW();
  if (environment) {
    for (const wchar_t* entry = environment; *entry;
         entry += std::wcslen(entry) + 1) {
      result.environment.push_back(utf8(entry, -1));
      if (!result.environment.back().empty())
        result.environment.back().pop_back();
    }
    FreeEnvironmentStringsW(environment);
  }
#elif defined(__APPLE__)
  for (int index = 0; index < *_NSGetArgc(); ++index) {
    result.args.emplace_back((*_NSGetArgv())[index]);
  }
  for (char** entry = *_NSGetEnviron(); entry && *entry; ++entry) {
    result.environment.emplace_back(*entry);
  }
#else
  std::ifstream args("/proc/self/cmdline", std::ios::binary);
  std::string item;
  while (std::getline(args, item, '\0')) result.args.push_back(item);
  for (char** entry = environ; entry && *entry; ++entry) {
    result.environment.emplace_back(*entry);
  }
#endif
  return result;
}

}  // namespace

Expected<StableConfig> load_stable_config(StringView language,
                                          StringView local_path,
                                          StringView fleet_path) {
  const std::string local =
      local_path.empty() ? local_default_path()
                         : std::string(local_path.data(), local_path.size());
  const std::string fleet =
      fleet_path.empty() ? fleet_default_path()
                         : std::string(fleet_path.data(), fleet_path.size());
  Expected<std::string> local_yaml = read_config_file(local, "local");
  if (Error* error = local_yaml.if_error()) return *error;
  Expected<std::string> fleet_yaml = read_config_file(fleet, "fleet");
  if (Error* error = fleet_yaml.if_error()) return *error;
  return stable_config_internal::load_yaml(*local_yaml, *fleet_yaml,
                                           current_process(language));
}

Expected<FinalizedTracerConfig> finalize_config_with_stable_config(
    const TracerConfig& config, StringView language, StringView local_path,
    StringView fleet_path) {
  Expected<StableConfig> stable_config =
      load_stable_config(language, local_path, fleet_path);
  if (Error* error = stable_config.if_error()) return *error;
  return finalize_config(config, *stable_config);
}

}  // namespace datadog::tracing
