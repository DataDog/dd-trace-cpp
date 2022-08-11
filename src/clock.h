#pragma once

#include <functional>

namespace datadog {
namespace tracing {

struct TimePoint {
  // TODO
};

using Clock = std::function<TimePoint()>;

extern Clock default_clock;

}  // namespace tracing
}  // namespace datadog
