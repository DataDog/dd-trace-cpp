#include <datadog/clock.h>
#include <datadog/curl.h>
#include <datadog/span_data.h>
#include <datadog/threaded_event_scheduler.h>
#include <datadog/tracer_config.h>

#include <chrono>
#include <fstream>
#include <iostream>
#include <thread>

namespace dd = datadog::tracing;

void play_with_msgpack();
void play_with_curl_and_event_scheduler();
void play_with_event_scheduler();
void play_with_curl();
void smoke();

int main() {
  play_with_msgpack();
  std::cout << "Done playing with msgpack.\n";

  // play_with_curl_and_event_scheduler();
  // std::cout << "Done playing with Curl and event scheduler.\n";

  // play_with_curl();
  // std::cout << "Done playing with Curl." << std::endl;

  // play_with_event_scheduler();
  // std::cout << "Done playing with event scheduler.\n";
}

void smoke() {
  dd::TracerConfig config;
  config.spans.service = "foosvc";
  std::cout << "config.spans.service: " << config.spans.service << '\n';

  std::get<dd::DatadogAgentConfig>(config.collector).http_client = nullptr;
}

void play_with_event_scheduler() {
  dd::ThreadedEventScheduler scheduler;

  const auto cancel1 = scheduler.schedule_recurring_event(
      std::chrono::seconds(3),
      []() { std::cout << "Here is your recurring event." << std::endl; });

  const auto cancel2 = scheduler.schedule_recurring_event(
      std::chrono::milliseconds(500),
      []() { std::cout << "Beep!" << std::endl; });

  std::this_thread::sleep_for(std::chrono::seconds(10));
  std::cout << "Cancelling\n";
  cancel1();
  std::this_thread::sleep_for(std::chrono::seconds(5));

  std::cout << "Shutting down\n";
}

void play_with_curl() {
  dd::Curl client;
  (void)client;

  dd::HTTPClient::URL url;
  url.scheme = "http";
  url.authority = "localhost";
  url.path = "/post";

  const auto set_headers = [](dd::DictWriter& headers) {
    headers.set("Content-Type", "text");
  };
  const std::string body = "Hello, world!";
  const auto on_response = [](int status, const dd::DictReader& headers,
                              std::string body) {
    std::cout << "Got response status " << status << '\n';
    headers.visit([](std::string_view key, std::string_view value) {
      std::cout << "Got response header " << key << " = " << value << '\n';
    });
    std::cout << "Got response body: " << body << std::endl;
  };
  const auto on_error = [](dd::Error error) {
    std::cout << "Got error code " << error.code << ": " << error.message
              << std::endl;
  };

  for (int i = 0; i < 10; ++i) {
    const auto error =
        client.post(url, set_headers, body, on_response, on_error);
    if (error) {
      std::cout << "Curl returned error " << error->code << ": "
                << error->message << std::endl;
    }
  }

  std::cin.get();
}

void play_with_curl_and_event_scheduler() {
  // Send a request every two seconds.
  // When the user enters input, cancel the event and shut down.

  dd::ThreadedEventScheduler scheduler;
  dd::Curl client;

  const auto cancel =
      scheduler.schedule_recurring_event(std::chrono::seconds(2), [&client]() {
        dd::HTTPClient::URL url;
        url.scheme = "http";
        url.authority = "localhost";
        url.path = "/post";

        const auto set_headers = [](dd::DictWriter& headers) {
          headers.set("Content-Type", "text");
        };
        const std::string body = "Hello, world!";
        const auto on_response = [](int status, const dd::DictReader& headers,
                                    std::string body) {
          std::cout << "Got response status " << status << '\n';
          headers.visit([](std::string_view key, std::string_view value) {
            std::cout << "Got response header " << key << " = " << value
                      << '\n';
          });
          std::cout << "Got response body: " << body << std::endl;
        };
        const auto on_error = [](dd::Error error) {
          std::cout << "Got error code " << error.code << ": " << error.message
                    << std::endl;
        };

        const auto error =
            client.post(url, set_headers, body, on_response, on_error);
        if (error) {
          std::cout << "Curl returned error " << error->code << ": "
                    << error->message << std::endl;
        }
      });

  std::cin.get();
  std::cout << "()()()() cancelling..." << std::endl;
  cancel();
  std::cout << "()()()() shutting down..." << std::endl;
}

void play_with_msgpack() {
  dd::SpanData span;
  span.trace_id = 123;
  span.span_id = 456;
  span.parent_id = 789;
  span.service = "foosvc";
  span.name = "do_thing";
  span.type = "web";
  span.tags["hello"] = "world";
  span.numeric_tags["thing"] = -0.34;
  span.start = dd::default_clock();
  span.duration = std::chrono::seconds(10);

  std::ofstream out("/tmp/span.msgpack");
  std::string buffer;
  dd::msgpack_encode(buffer, span);
  out.write(buffer.data(), buffer.size());

  std::cout << "span written to /tmp/span.msgpack\n";
}
