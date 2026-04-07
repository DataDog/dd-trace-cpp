#include <datadog/tracer_signature.h>

#include "root_session_id.h"

namespace datadog {
namespace tracing {

TracerSignature::TracerSignature(RuntimeID runtime_id,
                                 std::string default_service,
                                 std::string default_environment)
    : TracerSignature(runtime_id,
                      root_session_id::get_or_init(runtime_id.string()),
                      std::move(default_service),
                      std::move(default_environment)) {}

}  // namespace tracing
}  // namespace datadog
