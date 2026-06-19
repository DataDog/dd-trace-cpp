#include "otel_process_ctx_guard.h"

#include <datadog/logger.h>

#include <mutex>
#include <ostream>

namespace datadog {
namespace tracing {
namespace {

std::mutex& otel_ctx_mutex() {
  static std::mutex m;
  return m;
}

}  // namespace

OtelCtxGuard::~OtelCtxGuard() {
  std::lock_guard<std::mutex> lock(otel_ctx_mutex());
  otel_process_ctx_drop_current();
}

std::unique_ptr<OtelCtxGuard> publish_otel_process_ctx(
    const otel_process_ctx_data& data, Logger& logger) {
  std::lock_guard<std::mutex> lock(otel_ctx_mutex());
  const auto result = otel_process_ctx_publish(&data);
  if (!result.success) {
    logger.log_error([&](std::ostream& log) {
      log << "Failed to publish OpenTelemetry process context: "
          << result.error_message;
    });
    return nullptr;
  }
  return std::make_unique<OtelCtxGuard>();
}

}  // namespace tracing
}  // namespace datadog
