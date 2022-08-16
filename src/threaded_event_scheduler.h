#pragma once

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <functional>
#include <memory>
#include <mutex>
#include <queue>
#include <thread>
#include <vector>

#include "event_scheduler.h"

namespace datadog {
namespace tracing {

class ThreadedEventScheduler : public EventScheduler {
  struct EventConfig {
    std::function<void()> callback;
    std::chrono::steady_clock::duration interval;
    std::atomic_bool cancelled;

    EventConfig(std::function<void()> callback,
                std::chrono::steady_clock::duration interval);
  };

  struct ScheduledRun {
    std::chrono::steady_clock::time_point when;
    std::shared_ptr<const EventConfig> config;
  };

  struct GreaterThan {
    bool operator()(const ScheduledRun&, const ScheduledRun&) const;
  };

  std::mutex mutex_;
  ScheduledRun current_;
  std::condition_variable schedule_or_shutdown_;
  bool running_current_;
  std::condition_variable current_done_;
  std::priority_queue<ScheduledRun, std::vector<ScheduledRun>, GreaterThan>
      upcoming_;
  bool shutting_down_;
  std::thread dispatcher_;

  void run();

 public:
  // TODO: Didn't figure out what kind of dependency injection is appropriate
  // for a thread that interacts with a condition variable.
  ThreadedEventScheduler();
  ~ThreadedEventScheduler();

  virtual std::variant<Cancel, Error> schedule_recurring_event(
      std::chrono::steady_clock::duration interval,
      std::function<void()> callback) override;
};

}  // namespace tracing
}  // namespace datadog
