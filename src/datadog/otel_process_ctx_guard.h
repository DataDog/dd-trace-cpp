#pragma once

#include <memory>

#include "otel_process_ctx.h"

namespace datadog {
namespace tracing {

class Logger;

// RAII handle for a published OpenTelemetry process context. Destroying the
// guard drops the process-wide context.
//
// Multi-instance behavior: the OTel context is a per-process singleton.
// A successful publish replaces any previously-published context, and
// destroying any guard drops whatever is current (last writer wins style).
//
// This global state is serialized via an internal mutex so this class is
// thread-safe.
class OtelCtxGuard {
 public:
  ~OtelCtxGuard();
};

// Publish the OTel process context. Returns a guard whose destructor drops
// the context, or nullptr on failure (errors are logged via `logger`).
std::unique_ptr<OtelCtxGuard> publish_otel_process_ctx(
    const otel_process_ctx_data& data, Logger& logger);

}  // namespace tracing
}  // namespace datadog
