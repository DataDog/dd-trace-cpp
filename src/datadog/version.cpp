#include <datadog/version.h>

namespace datadog::tracing {

#define DD_TRACE_LIBRARY_NAME "dd-trace-cpp"
#define DD_TRACE_VERSION "v2.2.0"

const char* const tracer_library_name = DD_TRACE_LIBRARY_NAME;
const char* const tracer_version = DD_TRACE_VERSION;
const char* const tracer_version_string =
    "[" DD_TRACE_LIBRARY_NAME " version " DD_TRACE_VERSION "]";

}  // namespace datadog::tracing
