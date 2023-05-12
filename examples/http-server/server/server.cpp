// This is an HTTP service for a note-taking app. It's traced by Datadog via
// manual instrumentation using the dd-trace-cpp library.
//
// This service does its work by accessing a database provided by another
// service called "database".
//
// This service provides the following operations:
//
//     GET /notes
//         Return a JSON array of all stored notes, where each note is a JSON
//         array [created time, note], e.g. ["2023-05-12 12:38:25","here's a note"].
//
//     POST /notes
//         Create a new note.  The body of the request is the note content.
//
//     GET /sleep?seconds=<number>
//
//         Wait <number> seconds before responding. For example,
//
//             GET /sleep?seconds=0.023
//
//         will deliver a response after approximately 23 milliseconds.

#include <datadog/clock.h>
#include <datadog/dict_reader.h>
#include <datadog/dict_writer.h>
#include <datadog/sampling_decision.h>
#include <datadog/sampling_priority.h>
#include <datadog/span_config.h>
#include <datadog/trace_segment.h>
#include <datadog/tracer.h>
#include <datadog/tracer_config.h>

#include <cassert>
#include <charconv>
#include <csignal>
#include <cstdlib>
#include <iostream>
#include <iterator>
#include <ostream>
#include <sstream>
#include <stack>
#include <system_error>

#include "httplib.h"

// Alias the datadog namespace for brevity.
namespace dd = datadog::tracing;

// `hard_stop` is installed as a signal handler for `SIGTERM`.
// For some reason, the default handler was not being called.
void hard_stop(int /*signal*/) { std::exit(0); }

// `RequestTracingContext` is Datadog tracing specific information that is
// associated with each incoming request via `Request::user_data`.
struct RequestTracingContext {
  // `spans` is a stack of Datadog tracing spans.
  //
  // In a purely synchronous program, an explicit stack would not be necessary
  // because there's a stack implicit in the call stack, i.e. functions
  // calling functions. But because `httplib`, the HTTP library in use here,
  // exposes some events via callbacks, we need to store the spans somewhere
  // until they're finished, and so we use this `std::stack`.
  //
  // There will be at most two elements in `spans`: first the span that
  // represents the entire request (see `set_pre_request_handler`), and second
  // its child that represents reading the request body and dispatching to a
  // route-specific handler (see `set_pre_routing_handler`). The grandchild
  // span, corresponding to the route-specific handler, can live on the call
  // stack of the handler function, and so that span and its descendants are
  // never added to `stack`.
  //
  // Since there are at most two spans in `spans`, and because we know what
  // they are, we could instead have two data members of type
  // `std::optional<dd::Span>`, one for each of the two aforementioned spans.
  // They would need to be `std::optional` because sometimes one or both of
  // the spans is never created. Then we wouldn't need the stack.
  //
  // Even so, we use this `std::stack` in order to illustrate the RAII
  // behavior of `dd::Span`, and to emphasize that `std::optional` is not
  // always necessary, even in asynchronous scenarios. It also makes it
  // simpler to add additional layers of callbacks in the future.
  std::stack<dd::Span> spans;

  // `request_start` is the time that this request began. Specifically, it's
  // the beginning of the handler installed by `set_pre_request_handler`. The
  // reason we need to store this time is that we cannot create a `dd::Span`
  // immediately, because we don't know whether to extract trace context from
  // the caller until we've read the request headers. So, the pre-request
  // handler stores `request_start` time, and then later, after the request
  // headers are read, the pre-routing handler creates the initial span using
  // the `request_start` time.
  dd::TimePoint request_start;
};

// `HeaderReader` adapts dd-trace-cpp's reader interface to the HTTP headers
// object used by this app's HTTP library.
//
// dd-trace-cpp uses this to extract trace context from incoming HTTP request
// headers.
class HeaderReader : public dd::DictReader {
  const httplib::Headers& headers_;
  mutable std::string buffer_;

 public:
  explicit HeaderReader(const httplib::Headers& headers) : headers_(headers) {}

  std::optional<std::string_view> lookup(std::string_view key) const override {
    // If there's no matching header, then return `std::nullopt`.
    // If there is one matching header, then return a view of its value.
    // If there are multiple matching headers, then join their values with
    // commas and return a view of the result.
    const auto [begin, end] = headers_.equal_range(std::string{key});
    switch (std::distance(begin, end)) {
      case 0:
        return std::nullopt;
      case 1:
        return begin->second;
    }
    auto it = begin;
    buffer_ = it->second;
    ++it;
    do {
      buffer_ += ',';
      buffer_ += it->second;
      ++it;
    } while (it != end);
    return buffer_;
  }

  void visit(const std::function<void(std::string_view key, std::string_view value)>& visitor) const override {
    for (const auto& [key, value] : headers_) {
      visitor(key, value);
    }
  }
};

// `HeaderWriter` adapts dd-trace-cpp's writer interface to the HTTP headers
// object used by this app's HTTP library.
//
// dd-trace-cpp uses this to inject trace context into outgoing HTTP request
// headers.
class HeaderWriter : public dd::DictWriter {
  httplib::Headers& headers_;

 public:
  explicit HeaderWriter(httplib::Headers& headers) : headers_(headers) {}

  void set(std::string_view key, std::string_view value) override { headers_.emplace(key, value); }
};

// See the implementations of these functions, below `main`, for a description
// of what they do.
void on_request_begin(httplib::Request& request);
void on_request_headers_consumed(const httplib::Request& request, dd::Tracer& tracer);
void on_healthcheck(const httplib::Request& request, httplib::Response& response);
void on_sleep(const httplib::Request& request, httplib::Response& response);
void on_get_notes(const httplib::Request& request, httplib::Response& response);
void on_post_notes(const httplib::Request& request, httplib::Response& response);

int main() {
  // Set up the Datadog tracer.  See `src/datadog/tracer_config.h`.
  dd::TracerConfig config;
  config.defaults.service = "dd-trace-cpp-http-server-example-server";
  config.defaults.service_type = "server";

  // `finalize_config` validates `config` and applies any settings from
  // environment variables, such as `DD_AGENT_HOST`.
  // If the resulting configuration is valid, it will return a
  // `FinalizedTracerConfig` that can then be used to initialize a `Tracer`.
  // If the resulting configuration is invalid, then it will return an
  // `Error` that can be printed, but then no `Tracer` can be created.
  dd::Expected<dd::FinalizedTracerConfig> finalized_config = dd::finalize_config(config);
  if (dd::Error* error = finalized_config.if_error()) {
    std::cerr << "Error: Datadog is misconfigured. " << *error << '\n';
    return 1;
  }

  dd::Tracer tracer{*finalized_config};

  // Configure the HTTP server.
  httplib::Server server;

  // `httplib` provides a hook into when a request first begins. We call
  // `on_request_begin`, which installs a `RequestTracingContext` into the
  // request's `user_data`, so that subsequent callbacks (like the
  // route-specific request handlers below) have access to the tracing context
  // for this request.
  // There is a corresponding hook into when the request ends. See
  // `set_post_request_handler` below.
  server.set_pre_request_handler([](httplib::Request& request, httplib::Response&) { on_request_begin(request); });

  // `httplib` provides a hook into when request headers have been read, but
  // before the route-specific handler is called.
  // There is a corresponding hook into when the route-specific handler has
  // returned. See `set_post_routing_handler` below.
  server.set_pre_routing_handler([&](const httplib::Request& request, httplib::Response&) {
    on_request_headers_consumed(request, tracer);
    return httplib::Server::HandlerResponse::Unhandled;
  });

  server.Get("/healthcheck", on_healthcheck);  // handler for GET /healthcheck
  server.Get("/notes", on_get_notes);          // handler for GET /notes
  server.Post("/notes", on_post_notes);        // handler for POST /notes
  server.Get("/sleep", on_sleep);              // handler for GET /sleep

  // `httplib` provides a hook into when the route-specific handler (see above)
  // has finished.
  // Here we finish (destroy) one of the `dd::Span` objects that we previously
  // created. We finish it by popping it off of the span stack.
  server.set_post_routing_handler([](const httplib::Request& request, httplib::Response&) {
    auto* context = static_cast<RequestTracingContext*>(request.user_data.get());
    context->spans.pop();
    return httplib::Server::HandlerResponse::Unhandled;
  });

  // `httplib` provides a hook into when the the request is completely finished.
  // Here we finish (destroy) the last remaining, and toplevel, `dd::Span`
  // object that we previously created. We finish it by popping it off of the
  // span stack.
  server.set_post_request_handler([](const httplib::Request& request, httplib::Response& response) {
    auto* context = static_cast<RequestTracingContext*>(request.user_data.get());
    context->spans.top().set_tag("http.status_code", std::to_string(response.status));
    context->spans.pop();
  });

  // Run the HTTP server.
  std::signal(SIGTERM, hard_stop);
  server.listen("0.0.0.0", 80);
}

// TODO
void on_request_begin(httplib::Request& request) {
  const auto now = dd::default_clock();
  auto context = std::make_shared<RequestTracingContext>();
  context->request_start = now;
  request.user_data = std::move(context);
}

// TODO
void on_request_headers_consumed(const httplib::Request& request, dd::Tracer& tracer) {
  const auto now = dd::default_clock();
  auto* context = static_cast<RequestTracingContext*>(request.user_data.get());

  // Create the span corresponding to the entire handling of the request.
  dd::SpanConfig config;
  config.name = "handle.request";
  config.start = context->request_start;

  HeaderReader reader{request.headers};
  auto maybe_span = tracer.extract_or_create_span(reader, config);
  if (dd::Error* error = maybe_span.if_error()) {
    std::cerr << "While extracting trace context from request: " << *error << '\n';
    // Create a trace from scratch.
    context->spans.push(tracer.create_span(config));
  } else {
    context->spans.push(std::move(*maybe_span));
  }

  dd::Span& span = context->spans.top();
  span.set_resource_name(request.method + " " + request.path);
  span.set_tag("network.client.ip", request.remote_addr);
  span.set_tag("network.client.port", std::to_string(request.remote_port));
  span.set_tag("http.url_details.path", request.path);
  span.set_tag("http.method", request.method);

  // Create a span corresponding to reading the request body and executing
  // the route-specific handler.
  config.name = "route.request";
  config.start = now;
  context->spans.push(span.create_child(config));
}

// TODO
void on_healthcheck(const httplib::Request& request, httplib::Response& response) {
  auto* context = static_cast<RequestTracingContext*>(request.user_data.get());

  // We'd prefer not to send healthcheck traces to Datadog. They're
  // noisy. So, override the sampling decision to "definitely
  // drop," and don't even bother creating a span here.
  context->spans.top().trace_segment().override_sampling_priority(int(dd::SamplingPriority::USER_DROP));

  response.set_content("I'm still here!\n", "text/plain");
}

// TODO
void on_sleep(const httplib::Request& request, httplib::Response& response) {
  auto* context = static_cast<RequestTracingContext*>(request.user_data.get());

  dd::Span span = context->spans.top().create_child();
  span.set_name("sleep");
  span.set_tag("http.route", "/sleep");

  const auto [begin, end] = request.params.equal_range("seconds");
  if (std::distance(begin, end) != 1) {
    response.status = 400;  // "bad request"
    response.set_content("\"seconds\" query parameter must be specified exactly once.\n", "text/plain");
    return;
  }

  const std::string_view raw = begin->second;
  double seconds;
  const auto result = std::from_chars(raw.begin(), raw.end(), seconds);
  if (result.ec == std::errc::invalid_argument || result.ec == std::errc::result_out_of_range ||
      result.ptr != raw.end() || seconds < 0) {
    response.status = 400;  // "bad request"
    response.set_content(
        "\"seconds\" query parameter must be a non-negative number in the range of an IEEE754 double.\n", "text/plain");
    return;
  }

  using namespace std::chrono;
  std::this_thread::sleep_for(round<nanoseconds>(duration<double>(seconds)));
}

// TODO
httplib::Result traced_get(httplib::Client& client, const std::string& endpoint, const httplib::Params& params,
                           httplib::Headers& headers, dd::Span& parent_span) {
  dd::Span span = parent_span.create_child();
  span.set_name("http.client");
  span.set_resource_name("GET " + endpoint);
  // TODO: tags...
  HeaderWriter writer{headers};
  span.inject(writer);

  return client.Get(endpoint, params, headers);
}

// TODO
void on_get_notes(const httplib::Request& request, httplib::Response& response) {
  auto* context = static_cast<RequestTracingContext*>(request.user_data.get());

  dd::Span span = context->spans.top().create_child();
  span.set_name("get-notes");
  span.set_tag("http.route", "/notes");

  httplib::Client database("database", 80);
  httplib::Params params;
  params.emplace("sql", "select AddedWhen, Body from Note order by AddedWhen desc;");
  httplib::Headers headers;
  if (const auto result = traced_get(database, "/query", params, headers, span)) {
    response.status = result->status;
    response.set_content(result->body, result->get_header_value("Content-Type"));
  } else {
    response.status = 500;  // "internal server error"
  }
}

// TODO
// "It's true" -> "'It''s true'"
class sql_quote {
  const std::string& text_;

 public:
  explicit sql_quote(const std::string& text) : text_(text) {}

  friend std::ostream& operator<<(std::ostream& stream, const sql_quote& source) {
    stream.put('\'');
    for (char ch : source.text_) {
      stream.put(ch);
      if (ch == '\'') {
        stream.put(ch);
      }
    }
    stream.put('\'');
    return stream;
  }
};

// TODO
void on_post_notes(const httplib::Request& request, httplib::Response& response) {
  auto* context = static_cast<RequestTracingContext*>(request.user_data.get());

  dd::Span span = context->spans.top().create_child();
  span.set_name("add-note");
  span.set_tag("http.route", "/notes");
  span.set_tag("note", request.body);

  httplib::Client database("database", 80);
  httplib::Params params;
  std::ostringstream sql;
  sql << "insert into Note(AddedWhen, Body) values(datetime(), " << sql_quote(request.body) << ");";
  params.emplace("sql", sql.str());
  httplib::Headers headers;
  if (const auto result = traced_get(database, "/execute", params, headers, span)) {
    response.status = result->status;
    response.set_content(result->body, result->get_header_value("Content-Type"));
  } else {
    response.status = 500;  // "internal server error"
  }
}
