#pragma once

#include <chrono>
#include <functional>

#include "error.h"

namespace datadog {
namespace tracing {

class EventScheduler {
 public:
  using Cancel = std::function<void()>;

  virtual Cancel schedule_recurring_event(
      std::chrono::steady_clock::duration interval,
      std::function<void()> callback) = 0;

  virtual ~EventScheduler() = default;
};

}  // namespace tracing
}  // namespace datadog
