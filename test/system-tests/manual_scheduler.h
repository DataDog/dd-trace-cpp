#pragma once

#include <datadog/event_scheduler.h>
#include <datadog/threaded_event_scheduler.h>

#include <cassert>
#include <datadog/json.hpp>

struct ManualScheduler : public datadog::tracing::EventScheduler {
  datadog::tracing::ThreadedEventScheduler scheduler_;
  std::function<void()> flush_traces = nullptr;
  std::function<void()> flush_telemetry = nullptr;

  Cancel schedule_recurring_event(std::chrono::steady_clock::duration interval,
                                  std::function<void()> callback) override {
    assert(callback != nullptr);

    // NOTE: This depends on the precise order that dd-trace-cpp sets up the
    // `schedule_recurring_event`s for traces and telemetry.
    if (flush_traces == nullptr) {
      flush_traces = callback;
      return {};
    }
    if (flush_telemetry == nullptr) {
      flush_telemetry = callback;
      scheduler_.schedule_recurring_event(interval, callback);
    }
    return []() {};
  }

  nlohmann::json config_json() const override {
    return nlohmann::json::object({{"type", "ManualScheduler"}});
  }
};
