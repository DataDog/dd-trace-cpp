#pragma once

#include <chrono>
#include <functional>
#include <variant>

#include "error.h"

namespace datadog {
namespace tracing {

class EventScheduler {
 public:
  using Cancel = std::function<void()>;

  virtual std::variant<Cancel, Error> schedule_recurring_event(
      std::chrono::steady_clock::duration interval,
      std::function<void()> callback) = 0;
};

}  // namespace tracing
}  // namespace datadog
