#pragma once

#include <chrono>
#include <functional>

namespace datadog {
namespace tracing {

using Duration = std::chrono::steady_clock::duration;

struct TimePoint {
  std::chrono::system_clock::time_point wall =
      std::chrono::system_clock::time_point();
  std::chrono::steady_clock::time_point tick =
      std::chrono::steady_clock::time_point();
};

inline Duration operator-(const TimePoint& after, const TimePoint& before) {
  return after.tick - before.tick;
}

inline TimePoint operator-(const TimePoint& origin, Duration offset) {
  return {origin.wall - offset, origin.tick - offset};
}

inline TimePoint& operator+=(TimePoint& self, Duration offset) {
  self.wall += offset;
  self.tick += offset;
  return self;
}

using Clock = std::function<TimePoint()>;

extern const Clock default_clock;

}  // namespace tracing
}  // namespace datadog
