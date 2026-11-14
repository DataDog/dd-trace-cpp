#include <datadog/optional.h>
#include <datadog/string_view.h>

#include <array>
#include <string>

namespace datadog::telemetry {

enum class LogLevel : char { ERROR, WARNING };

// The names of the `level` tag of the logs_created metric.
constexpr std::array<const char*, 2> tag_value_names(LogLevel) {
  return {"error", "warning"};
}

struct LogMessage final {
  std::string message;
  LogLevel level;
  tracing::Optional<std::string> stacktrace;
  std::chrono::seconds::rep timestamp;
};

inline tracing::StringView to_string(LogLevel level) {
  switch (level) {
    case LogLevel::ERROR:
      return "ERROR";
    case LogLevel::WARNING:
      return "WARNING";
  }

  // Unreachable.
  return "";
}

}  // namespace datadog::telemetry
