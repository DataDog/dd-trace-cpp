#include "logger.h"

#include "error.h"

namespace datadog {
namespace tracing {

void Logger::log_error(const Error& error) {
  log_error([&](auto& stream) { stream << error; });
}

void Logger::log_error(StringView message) {
  log_error([&](auto& stream) { stream << message; });
}

void Logger::log_debug(const LogFunc&) {}

void Logger::log_debug(StringView message) {
  log_debug([message](auto& stream) { stream << message; });
}

}  // namespace tracing
}  // namespace datadog
