#include "version.h"

namespace datadog {
namespace tracing {

#define VERSION "v0.2.0"

const char* const tracer_version = VERSION;
const char* const tracer_version_string = "[dd-trace-cpp version " VERSION "]";

}  // namespace tracing
}  // namespace datadog
