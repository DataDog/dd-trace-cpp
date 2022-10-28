#pragma once

// This component provides the release version of this library.
// `tracer_version` is sent to the Datadog Agent with each trace, and is printed
// to the log whenever `Tracer` is initialized.

#include <string_view>

namespace datadog {
namespace tracing {

extern const std::string_view tracer_version;

}  // namespace tracing
}  // namespace datadog
