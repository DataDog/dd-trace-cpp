#include <datadog/clock.h>
#include <datadog/collector_response.h>
#include <datadog/curl.h>
#include <datadog/datadog_agent.h>
#include <datadog/span.h>
#include <datadog/span_config.h>
#include <datadog/span_data.h>
#include <datadog/tags.h>
#include <datadog/threaded_event_scheduler.h>
#include <datadog/trace_segment.h>
#include <datadog/tracer.h>
#include <datadog/tracer_config.h>

#include <cassert>
#include <chrono>
#include <cstddef>
#include <fstream>
#include <iostream>
#include <memory>
#include <string_view>
#include <thread>

namespace dd = datadog::tracing;

void play_with_agent();
void play_with_parse_url();
void play_with_span_tags();
void play_with_create_span();
void play_with_config();
void play_with_cpp20_syntax();
void play_with_msgpack();
void play_with_curl_and_event_scheduler();
void play_with_event_scheduler();
void play_with_curl();
void smoke();

void usage(const char* argv0);

int main(int argc, char* argv[]) {
  if (argc < 2) {
    usage(argv[0]);
    return 1;
  }

  for (const char* const* arg = argv + 1; *arg; ++arg) {
    const std::string_view example = *arg;

    if (example == "agent") {
      play_with_agent();
      std::cout << "\nDone playing with agent.\n";
    } else if (example == "parse_url") {
      play_with_parse_url();
      std::cout << "\nDone playing with parsing URLs.\n";
    } else if (example == "span_tags") {
      play_with_span_tags();
      std::cout << "\nDone playing with span tags.\n";
    } else if (example == "create_span") {
      play_with_create_span();
      std::cout << "\nDone playing with create_span.\n";
    } else if (example == "config") {
      play_with_config();
      std::cout << "\nDone playing with config.\n";
    } else if (example == "cpp20_syntax") {
      play_with_cpp20_syntax();
      std::cout << "\nDone playing with C++20 syntax.\n";
    } else if (example == "msgpack") {
      play_with_msgpack();
      std::cout << "\nDone playing with msgpack.\n";
    } else if (example == "curl_and_event_scheduler") {
      play_with_curl_and_event_scheduler();
      std::cout << "\nDone playing with Curl and event scheduler.\n";
    } else if (example == "curl") {
      play_with_curl();
      std::cout << "\nDone playing with Curl." << std::endl;
    } else if (example == "event_scheduler") {
      play_with_event_scheduler();
      std::cout << "\nDone playing with event scheduler.\n";
    } else {
      std::cerr << "Unknown example: " << example << '\n';
    }
  }
}

void usage(const char* argv0) {
  std::cerr << "usage: " << argv0 << " EXAMPLE_NAME\n";
}

void smoke() {
  dd::TracerConfig config;
  config.defaults.service = "foosvc";
  std::cout << "config.spans.service: " << config.defaults.service << '\n';

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
  span.service_type = "web";
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

void play_with_cpp20_syntax() {
  const auto print_service = [](const dd::SpanConfig& config) {
    std::cout << "service: " << config.service.value_or("<null>") << '\n';
  };

  // C++20 only, and even then GCC -Wextra complains about missing field
  // initializers.
  // print_service(dd::SpanConfig{.service = "hello"});

  dd::SpanConfig config;
  config.service = "hello";
  print_service(config);
}

void play_with_config() {
  const auto http_client = std::make_shared<dd::Curl>();

  {
    dd::TracerConfig raw_config;
    raw_config.defaults.service = "hello";

    auto& agent_config = std::get<dd::DatadogAgentConfig>(raw_config.collector);
    agent_config.http_client = http_client;

    auto maybe_config = dd::validate_config(raw_config);
    if (const auto* const error = std::get_if<dd::Error>(&maybe_config)) {
      std::cout << "Bad config: " << error->message << '\n';
      return;
    }

    dd::Tracer tracer{std::get<dd::Validated<dd::TracerConfig>>(maybe_config)};
    (void)tracer;
  }

  {
    dd::TracerConfig raw_config;
    // raw_config.defaults.service = "hello";
    auto maybe_config = dd::validate_config(raw_config);
    if (const auto* const error = std::get_if<dd::Error>(&maybe_config)) {
      std::cout << "Bad config: " << error->message << '\n';
      return;
    }

    dd::Tracer tracer{std::get<dd::Validated<dd::TracerConfig>>(maybe_config)};
  }

  // "error: use of deleted function
  // ‘datadog::tracing::Validated<TracerConfig>::ValidatedTracerConfig()’"
  // dd::Validated<TracerConfig> config;
  // (void) config;
}

void play_with_create_span() {
  const auto http_client = std::make_shared<dd::Curl>();

  dd::TracerConfig config;
  config.defaults.service = "hello";

  auto& agent_config = std::get<dd::DatadogAgentConfig>(config.collector);
  agent_config.http_client = http_client;

  auto maybe_config = dd::validate_config(config);
  if (const auto* const error = std::get_if<dd::Error>(&maybe_config)) {
    std::cout << "Bad config: " << error->message << '\n';
    return;
  }

  dd::Tracer tracer{std::get<dd::Validated<dd::TracerConfig>>(maybe_config)};
  dd::Span span = tracer.create_span(dd::SpanConfig{});

  dd::Span child = span.create_child(dd::SpanConfig{});
  child.trace_segment().visit_spans(
      [](const std::vector<std::unique_ptr<dd::SpanData>>& spans) {
        std::cout << "There are " << spans.size() << " spans in the trace.\n";
      });
}

void play_with_span_tags() {
  const auto http_client = std::make_shared<dd::Curl>();

  dd::TracerConfig config;
  config.defaults.service = "hello";

  auto& agent_config = std::get<dd::DatadogAgentConfig>(config.collector);
  agent_config.http_client = http_client;

  auto maybe_config = dd::validate_config(config);
  if (const auto* const error = std::get_if<dd::Error>(&maybe_config)) {
    std::cout << "Bad config: " << error->message << '\n';
    return;
  }

  dd::Tracer tracer{std::get<dd::Validated<dd::TracerConfig>>(maybe_config)};
  dd::Span span = tracer.create_span(dd::SpanConfig{});

  span.set_tag("foo", "bar");
  span.set_tag("foo", "I am foo");
  span.set_tag("hello.world", "123");
  auto lookup_result = span.lookup_tag("chicken");
  std::cout << "result of looking up \"chicken\": "
            << lookup_result.value_or("<not_found>") << '\n';
  lookup_result = span.lookup_tag("foo");
  std::cout << "result of looking up \"foo\": "
            << lookup_result.value_or("<not_found>") << '\n';
  lookup_result = span.lookup_tag("hello.world");
  std::cout << "result of looking up \"hello.world\": "
            << lookup_result.value_or("<not_found>") << '\n';
  std::cout << "Removing \"foo\"...\n";
  span.remove_tag("foo");
  lookup_result = span.lookup_tag("foo");
  std::cout << "result of looking up \"foo\": "
            << lookup_result.value_or("<not_found>") << '\n';
}

void play_with_parse_url() {
  const auto try_url = [](std::string_view raw) {
    std::cout << raw << "\n  ->  ";
    const auto result = dd::DatadogAgentConfig::parse(raw);
    struct Visitor {
      void operator()(const dd::HTTPClient::URL& url) const {
        std::cout << url;
      }
      void operator()(const dd::Error error) const { std::cout << error; }
    };
    std::visit(Visitor(), result);
    std::cout << "\n\n";
  };

  try_url("");
  try_url("smtp://fred@flinstones.cc");
  try_url("http://google.com");
  try_url("http://staging.datadog.hq/something/or/another");
  try_url("http://dd-agent:8126/api/v0.4/traces");
  try_url("unix:///var/run/dd-agent.sock");
  try_url("http+unix://var/run/dd-agent.sock");

  const auto http_client = std::make_shared<dd::Curl>();
  dd::DatadogAgentConfig config;
  config.http_client = http_client;
  config.agent_url = "unix://var/run/i.did.it.wrong.sock";
  std::cout << config.agent_url << "\n  ->  ";
  auto result = validate_config(config);
  if (auto* error = std::get_if<dd::Error>(&result)) {
    std::cout << *error;
  } else {
    const auto& validated = std::get<0>(result);
    std::cout << validated.agent_url;
  }
  std::cout << '\n';
}

void play_with_agent() {
  const auto scheduler = std::make_shared<dd::ThreadedEventScheduler>();
  const auto http_client = std::make_shared<dd::Curl>();
  dd::DatadogAgentConfig config;
  config.http_client = http_client;
  config.event_scheduler = scheduler;

  const auto result = dd::validate_config(config);
  const auto* validated =
      std::get_if<dd::Validated<dd::DatadogAgentConfig>>(&result);
  assert(validated);
  dd::DatadogAgent collector{*validated};

  std::ifstream dev_urandom{"/dev/urandom", std::ios::binary};
  const auto rand_uint64 = [&dev_urandom]() {
    char buffer[sizeof(std::uint64_t)];
    dev_urandom.read(buffer, sizeof buffer);
    assert(dev_urandom);
    return *reinterpret_cast<std::uint64_t*>(&buffer[0]);
  };

  const auto cancel = scheduler->schedule_recurring_event(
      std::chrono::milliseconds(1000), [&]() {
        // Create a trace having two spans, and then send it to the collector.
        const auto now = dd::default_clock();
        auto parent = std::make_unique<dd::SpanData>();
        parent->start = now;
        parent->duration = std::chrono::seconds(1);
        parent->trace_id = rand_uint64();
        parent->span_id = parent->trace_id;
        parent->parent_id = 0;
        parent->service = "dd-trace-cpp-example";
        parent->name = "do.thing";
        parent->tags[dd::tags::environment] = "dev";

        auto child = std::make_unique<dd::SpanData>();
        child->start = parent->start;
        child->duration = std::chrono::milliseconds(200);
        child->trace_id = parent->trace_id;
        child->span_id = rand_uint64();
        child->parent_id = parent->span_id;
        child->service = "dd-trace-cpp-example";
        child->name = "do.another.thing";
        child->tags[dd::tags::environment] = "dev";
        child->tags["editorial.note"] = "I'm the spicy one.";

        std::vector<std::unique_ptr<dd::SpanData>> chunk;
        chunk.push_back(std::move(parent));
        chunk.push_back(std::move(child));

        collector.send(
            std::move(chunk), [](const dd::CollectorResponse& response) {
              std::cout
                  << "Collector called my response handler.  The response has "
                  << response.sample_rate_by_key.size() << " elements:";
              for (const auto& [key, rate] : response.sample_rate_by_key) {
                std::cout << " \"" << key << "\"=" << rate;
              }
              std::cout << '\n';
            });
      });

  std::cin.get();
  cancel();
}
