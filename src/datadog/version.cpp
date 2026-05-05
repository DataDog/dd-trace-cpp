#include <datadog/version.h>

namespace datadog::tracing {

#define DD_TRACE_VERSION "v2.1.0"

const char* const tracer_version = DD_TRACE_VERSION;
const char* const tracer_version_string =
    "[dd-trace-cpp version " DD_TRACE_VERSION "]";

}  // namespace datadog::tracing
