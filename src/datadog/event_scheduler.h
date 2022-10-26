#pragma once

// This component provides an interface, `EventScheduler`, that allows a
// specified function-like object to be invoked at regular intervals.
//
// `DatadogAgent` uses an `EventScheduler` to periodically send batches of
// traces to the Datadog Agent.
//
// The default implementation is `ThreadedEventScheduler`.  See
// `threaded_event_scheduler.h`.

#include <chrono>
#include <functional>

#include "error.h"

namespace datadog {
namespace tracing {

class EventScheduler {
 public:
  using Cancel = std::function<void()>;

  // Invoke the specified `callback` repeatedly, with the specified `interval`
  // elapsing between invocations.  The first invocation is after an initial
  // `interval`.  Return a function-like object that can be invoked without
  // arguments to prevent subsequent invocations of `callback`.
  virtual Cancel schedule_recurring_event(
      std::chrono::steady_clock::duration interval,
      std::function<void()> callback) = 0;

  virtual ~EventScheduler() = default;
};

}  // namespace tracing
}  // namespace datadog
