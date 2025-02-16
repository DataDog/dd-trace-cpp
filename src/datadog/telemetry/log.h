#include <string>

namespace datadog::telemetry {

enum class LogLevel : char { ERROR, WARNING };

struct LogMessage final {
  std::string message;
  LogLevel level;
};

}  // namespace datadog::telemetry
