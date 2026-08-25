#pragma once

#include <string>

namespace datadog {
namespace tracing {

struct OtelTraceState {
  // Raw value of the `ot` tracestate member.
  std::string value;
  // Number of other-vendor members to emit before `ot`.
  std::size_t position;
};

}  // namespace tracing
}  // namespace datadog
