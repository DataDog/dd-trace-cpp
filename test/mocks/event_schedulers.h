#pragma once

#include <datadog/event_scheduler.h>

#include <chrono>
#include <functional>
#include <optional>

using namespace datadog::tracing;

struct MockEventScheduler : public EventScheduler {
  std::function<void()> event_callback;
  std::optional<std::chrono::steady_clock::duration> recurrence_interval;
  bool cancelled = false;

  Cancel schedule_recurring_event(std::chrono::steady_clock::duration interval,
                                  std::function<void()> callback) override {
    event_callback = callback;
    recurrence_interval = interval;
    return [this]() { cancelled = true; };
  }
};
