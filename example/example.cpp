#include <datadog/tracer_config.h>
#include <datadog/threaded_event_scheduler.h>

#include <chrono>
#include <iostream>
#include <thread>

namespace dd = datadog::tracing;

void play_with_event_scheduler();

int main() {
    dd::TracerConfig config;
    config.spans.service = "foosvc";
    std::cout << "config.spans.service: " << config.spans.service << '\n';
    
    std::get<dd::DatadogAgentConfig>(config.collector).http_client = nullptr;
    
    play_with_event_scheduler();
    std::cout << "Done playing with event scheduler.\n";
}

void play_with_event_scheduler() {
    dd::ThreadedEventScheduler scheduler;

    const auto cancel1 = scheduler.schedule_recurring_event(std::chrono::seconds(3), []() {
        std::cout << "Here is your recurring event." << std::endl;
    });

    const auto cancel2 = scheduler.schedule_recurring_event(std::chrono::milliseconds(500), []() {
        std::cout << "Beep!" << std::endl;
    });

    std::this_thread::sleep_for(std::chrono::seconds(10));
    std::cout << "Cancelling\n";
    cancel1();
    std::this_thread::sleep_for(std::chrono::seconds(5));

    std::cout << "Shutting down\n";
}
