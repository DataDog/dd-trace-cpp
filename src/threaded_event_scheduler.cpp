#include "threaded_event_scheduler.h"

#include <thread>

namespace datadog {
namespace tracing {

ThreadedEventScheduler::EventConfig::EventConfig(
    std::function<void()> callback,
    std::chrono::steady_clock::duration interval)
    : callback(callback), interval(interval), cancelled(false) {}

bool ThreadedEventScheduler::GreaterThan::operator()(
    const ScheduledRun& left, const ScheduledRun& right) const {
  return left.when > right.when;
};

ThreadedEventScheduler::ThreadedEventScheduler()
    : shutting_down_(false), dispatcher_([this]() { run(); }) {}

ThreadedEventScheduler::~ThreadedEventScheduler() {
  {
    std::lock_guard guard(mutex_);
    shutting_down_ = true;
    condition_.notify_one();
  }
  dispatcher_.join();
}

std::variant<EventScheduler::Cancel, Error>
ThreadedEventScheduler::schedule_recurring_event(
    std::chrono::steady_clock::duration interval,
    std::function<void()> callback) {
  const auto now = std::chrono::steady_clock::now();
  auto config = std::make_shared<EventConfig>(std::move(callback), interval);

  {
    std::lock_guard<std::mutex> guard(mutex_);
    upcoming_.push(ScheduledRun{now + interval, config});
    condition_.notify_one();
  }

  // Return a cancellation function.
  return EventScheduler::Cancel([config = std::move(config)]() mutable {
    if (config) {
      config->cancelled = true;
      config.reset();
    }
  });
}

void ThreadedEventScheduler::run() {
  ScheduledRun current;
  std::unique_lock<std::mutex> lock(mutex_);

  for (;;) {
    if (upcoming_.empty()) {
      // Nothing to do.  Wait until either of
      // `schedule_recurring_event` or the destructor pokes us.
      condition_.wait(
          lock, [this]() { return shutting_down_ || !upcoming_.empty(); });
      if (shutting_down_) {
        return;
      }
    }

    current = upcoming_.top();

    if (current.config->cancelled) {
      upcoming_.pop();
      continue;
    }

    const bool changed =
        condition_.wait_until(lock, current.when, [this, &current]() {
          return shutting_down_ || upcoming_.top().config != current.config;
        });

    if (shutting_down_) {
      return;
    }

    if (changed) {
      // A more recent event has been scheduled.
      continue;
    }

    // We waited for `current` and now it's its turn.
    upcoming_.pop();
    if (current.config->cancelled) {
      continue;
    }

    upcoming_.push(
        ScheduledRun{current.when + current.config->interval, current.config});
    lock.unlock();
    current.config->callback();
    lock.lock();
  }
}

}  // namespace tracing
}  // namespace datadog
