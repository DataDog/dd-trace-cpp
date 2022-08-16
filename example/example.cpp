#include <datadog/curl.h>
#include <datadog/threaded_event_scheduler.h>
#include <datadog/tracer_config.h>

#include <chrono>
#include <iostream>
#include <thread>

namespace dd = datadog::tracing;

void play_with_event_scheduler();
void play_with_curl();

int main() {
  dd::TracerConfig config;
  config.spans.service = "foosvc";
  std::cout << "config.spans.service: " << config.spans.service << '\n';

  std::get<dd::DatadogAgentConfig>(config.collector).http_client = nullptr;

  play_with_curl();
  std::cout << "Done playing with Curl.\n";

  // play_with_event_scheduler();
  // std::cout << "Done playing with event scheduler.\n";
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
  // https://httpbin.org/post
  /*
virtual std::optional<Error> post(const URL& url, HeadersSetter set_headers,
                                  std::string body,
                                  ResponseHandler on_response,
                                  ErrorHandler on_error) = 0;
  */
  dd::HTTPClient::URL url;
  url.scheme = "https";
  url.authority = "httpbin.org";
  url.path = "/post";

  const auto set_headers = [](dd::DictWriter& headers) {
    headers.set("Content-Type", "text");
  };
  const std::string body = "Hello, world!";
  const auto on_response = [](int status, const dd::DictReader& headers,
                              std::stringstream& body) {
    std::cout << "Got response status " << status << '\n';
    headers.visit([](std::string_view key, std::string_view value) {
      std::cout << "Got response header " << key << " = " << value << '\n';
    });
    std::cout << "Got response body: " << body.rdbuf() << std::endl;
  };
  const auto on_error = [](dd::Error error) {
    std::cout << "Got error code " << error.code << ": " << error.message
              << std::endl;
  };

  const auto error = client.post(url, set_headers, body, on_response, on_error);
  if (error) {
    std::cout << "Curl returned error " << error->code << ": " << error->message
              << std::endl;
  }

  std::cin.get();
}
