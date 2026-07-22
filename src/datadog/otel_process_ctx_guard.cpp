#include "otel_process_ctx_guard.h"

#include <datadog/logger.h>

#include <mutex>
#include <ostream>

namespace datadog::tracing {
namespace {

std::mutex& get_otel_context_mutex() {
  static std::mutex mutex;
  return mutex;
}

}  // namespace

OtelCtxGuard::~OtelCtxGuard() {
  std::lock_guard<std::mutex> lock(get_otel_context_mutex());
  otel_process_ctx_drop_current();
}

std::unique_ptr<OtelCtxGuard> publish_otel_process_ctx(
    const otel_process_ctx_data& data, Logger& logger) {
#ifndef __linux__
  // Feature is Linux-only, nothing to do elsewhere
  (void)data;
  (void)logger;
  return nullptr;
#else
  std::lock_guard<std::mutex> lock(get_otel_context_mutex());
  const auto result = otel_process_ctx_publish(&data);
  if (!result.success) {
    logger.log_error([&](std::ostream& log) {
      log << "Failed to publish OpenTelemetry process context: "
          << result.error_message;
    });
    return nullptr;
  }
  return std::make_unique<OtelCtxGuard>();
#endif
}

}  // namespace datadog::tracing
