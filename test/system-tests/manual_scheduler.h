#pragma once

#include <datadog/event_scheduler.h>
#include <datadog/threaded_event_scheduler.h>

#include <cassert>
#include <nlohmann/json.hpp>

struct ManualScheduler : public datadog::tracing::EventScheduler {
  std::function<void()> flush_traces = nullptr;
  datadog::tracing::ThreadedEventScheduler scheduler_;

  Cancel schedule_recurring_event(std::chrono::steady_clock::duration interval,
                                  std::function<void()> callback) override {
    assert(callback != nullptr);
    // This depends on DatadogAgent scheduling the traces flush first!
    if (flush_traces == nullptr) {
      flush_traces = callback;
      return []() {};
    }
    return scheduler_.schedule_recurring_event(interval, callback);
  }

  std::string config() const override {
    return nlohmann::json::object({{"type", "ManualScheduler"}}).dump();
  }
};
