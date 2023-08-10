#include "datadog_agent.h"

#include <cassert>
#include <chrono>
#include <string>
#include <typeinfo>
#include <unordered_map>
#include <unordered_set>

#include "collector_response.h"
#include "datadog_agent_config.h"
#include "debug_span.h"
#include "dict_writer.h"
#include "json.hpp"
#include "logger.h"
#include "msgpack.h"
#include "span_config.h"
#include "span_data.h"
#include "trace_sampler.h"
#include "tracer.h"
#include "version.h"

namespace datadog {
namespace tracing {
namespace {

const StringView traces_api_path = "/v0.4/traces";

HTTPClient::URL traces_endpoint(const HTTPClient::URL& agent_url) {
  auto traces_url = agent_url;
  append(traces_url.path, traces_api_path);
  return traces_url;
}

Expected<void> msgpack_encode(
    std::string& destination,
    const std::vector<DatadogAgent::TraceChunk>& trace_chunks,
    const Span* debug_parent) {
  return msgpack::pack_array(
      destination, trace_chunks,
      [&](std::string& destination, const DatadogAgent::TraceChunk& chunk) {
        DebugSpan debug{debug_parent};
        const auto size_before = destination.size();
        auto result = msgpack_encode(destination, chunk.spans, debug.get());
        const auto size_after = destination.size();
        debug.apply([&](Span& span) {
          span.set_name("encode.chunk");
          assert(!chunk.spans.empty());
          span.set_tag("metatrace.trace_id",
                       std::to_string(chunk.spans.front()->trace_id.low));
          span.set_tag("metatrace.chunk.span_count",
                       std::to_string(chunk.spans.size()));
          if (const Error* error = result.if_error()) {
            span.set_error_message(error->message);
          } else {
            span.set_tag("metatrace.chunk.encoded_size",
                         std::to_string(size_after - size_before));
          }
        });
        return result;
      });
}

std::variant<CollectorResponse, std::string> parse_agent_traces_response(
    StringView body) try {
  nlohmann::json response = nlohmann::json::parse(body);

  StringView type = response.type_name();
  if (type != "object") {
    std::string message;
    message +=
        "Parsing the Datadog Agent's response to traces we sent it failed.  "
        "The response is expected to be a JSON object, but instead it's a JSON "
        "value with type \"";
    append(message, type);
    message += '\"';
    message += "\nError occurred for response body (begins on next line):\n";
    append(message, body);
    return message;
  }

  const StringView sample_rates_property = "rate_by_service";
  const auto found = response.find(sample_rates_property);
  if (found == response.end()) {
    return CollectorResponse{};
  }
  const auto& rates_json = found.value();
  type = rates_json.type_name();
  if (type != "object") {
    std::string message;
    message +=
        "Parsing the Datadog Agent's response to traces we sent it failed.  "
        "The \"";
    append(message, sample_rates_property);
    message +=
        "\" property of the response is expected to be a JSON object, but "
        "instead it's a JSON value with type \"";
    append(message, type);
    message += '\"';
    message += "\nError occurred for response body (begins on next line):\n";
    append(message, body);
    return message;
  }

  std::unordered_map<std::string, Rate> sample_rates;
  for (const auto& [key, value] : rates_json.items()) {
    type = value.type_name();
    if (type != "number") {
      std::string message;
      message +=
          "Datadog Agent response to traces included an invalid sample rate "
          "for the key \"";
      message += key;
      message += "\". Rate should be a number, but it's a \"";
      append(message, type);
      message += "\" instead.";
      message += "\nError occurred for response body (begins on next line):\n";
      append(message, body);
      return message;
    }
    auto maybe_rate = Rate::from(value);
    if (auto* error = maybe_rate.if_error()) {
      std::string message;
      message +=
          "Datadog Agent response trace traces included an invalid sample rate "
          "for the key \"";
      message += key;
      message += "\": ";
      message += error->message;
      message += "\nError occurred for response body (begins on next line):\n";
      append(message, body);
      return message;
    }
    sample_rates.emplace(key, *maybe_rate);
  }
  return CollectorResponse{std::move(sample_rates)};
} catch (const nlohmann::json::exception& error) {
  std::string message;
  message +=
      "Parsing the Datadog Agent's response to traces we sent it failed with a "
      "JSON error: ";
  message += error.what();
  message += "\nError occurred for response body (begins on next line):\n";
  append(message, body);
  return message;
}

template <typename T>
bool empty(const std::weak_ptr<T>& ptr) {
  using Ptr = std::weak_ptr<T>;
  // `weak_ptr` has a "less than" operation. A pointer is empty if it is
  // equivalent to the default initialized pointer, i.e. if neither is less than
  // the other.
  return !ptr.owner_before(Ptr{}) && !Ptr{}.owner_before(ptr);
}

// `RequestContext` is any data that needs to be shared between our HTTP
// request's response and error handlers.
struct RequestContext {
  std::vector<DatadogAgent::TraceChunk> chunks;
  std::shared_ptr<Logger> logger;
  Optional<Span> debug_flush;
  Optional<Span> debug_request;
  Clock clock;
};

}  // namespace

DatadogAgent::DatadogAgent(const FinalizedDatadogAgentConfig& config,
                           const Clock& clock,
                           const std::shared_ptr<Logger>& logger)
    : clock_(clock),
      logger_(logger),
      traces_endpoint_(traces_endpoint(config.url)),
      http_client_(config.http_client),
      event_scheduler_(config.event_scheduler),
      cancel_scheduled_flush_(event_scheduler_->schedule_recurring_event(
          config.flush_interval, [this]() { flush(); })),
      flush_interval_(config.flush_interval) {
  assert(logger_);
}

DatadogAgent::~DatadogAgent() {
  const auto deadline = clock_().tick + std::chrono::seconds(2);
  cancel_scheduled_flush_();
  flush();
  http_client_->drain(deadline);
}

Expected<void> DatadogAgent::send(
    std::vector<std::unique_ptr<SpanData>>&& spans,
    const std::shared_ptr<TraceSampler>& response_handler) {
  Optional<Span> no_debug;
  return send(std::move(spans), response_handler, std::move(no_debug));
}

Expected<void> DatadogAgent::send(
    std::vector<std::unique_ptr<SpanData>>&& spans,
    const std::shared_ptr<TraceSampler>& response_handler,
    Optional<Span>&& debug_parent) {
  std::lock_guard<std::mutex> lock(mutex_);
  TraceChunk chunk;
  chunk.spans = std::move(spans);
  chunk.response_handler = response_handler;
  if (debug_parent) {
    SpanConfig config;
    config.name = "chunk.enqueued";
    chunk.debug_enqueued.emplace(debug_parent->create_child(config));
    chunk.debug_parent.emplace(std::move(*debug_parent));
  }
  trace_chunks_.push_back(std::move(chunk));
  return nullopt;
}

nlohmann::json DatadogAgent::config_json() const {
  const auto& url = traces_endpoint_;  // brevity
  const auto flush_interval_milliseconds =
      std::chrono::duration_cast<std::chrono::milliseconds>(flush_interval_)
          .count();

  // clang-format off
  auto result = nlohmann::json::object({
    {"type", "datadog::tracing::DatadogAgent"},
    {"config", nlohmann::json::object({
      {"url", (url.scheme + "://" + url.authority + url.path)},
      {"flush_interval_milliseconds", flush_interval_milliseconds},
      {"http_client", http_client_->config_json()},
      {"event_scheduler", event_scheduler_->config_json()},
    })},
  });
  // clang-format on

  if (!empty(debug_tracer_)) {
    result["config"]["has_debug_tracer"] = true;
  }

  return result;
}

void DatadogAgent::install_debug_tracer(
    const std::weak_ptr<Tracer>& debug_tracer) {
  debug_tracer_ = debug_tracer;
}

void DatadogAgent::flush() {
  const auto start = clock_();
  const auto request_context = std::make_shared<RequestContext>();
  request_context->logger = logger_;
  request_context->clock = clock_;
  std::vector<TraceChunk>& trace_chunks = request_context->chunks;
  Optional<Span>& debug_span = request_context->debug_flush;
  {
    std::lock_guard<std::mutex> lock(mutex_);
    using std::swap;
    swap(trace_chunks, trace_chunks_);
  }

  if (trace_chunks.empty()) {
    return;
  }

  // Create a debug trace if both we have a debug tracer and any of the
  // `trace_chunks` includes a debug span.
  if (!empty(debug_tracer_)) {
    for (auto& chunk : trace_chunks) {
      if (!chunk.debug_parent) {
        continue;
      }

      if (!debug_span) {
        SpanConfig config;
        config.name = "flush";
        config.start = start;
        // TODO: anything else?
        debug_span.emplace(debug_tracer_.lock()->create_span(config));
      }

      SpanConfig config;
      config.name = "flush";
      config.start = start;
      // TODO: 128-bit shenanigans?
      config.tags["metatrace.link.trace_id"] =
          std::to_string(debug_span->trace_id().low);
      config.tags["metatrace.link.span_id"] = std::to_string(debug_span->id());
      chunk.debug_span.emplace(chunk.debug_parent->create_child(config));

      chunk.debug_enqueued->set_end_time(start.tick);
      chunk.debug_enqueued.reset();
    }
  }

  std::string body;
  {
    DebugSpan debug_encode{debug_span};
    debug_encode.apply([](Span& span) { span.set_name("encode.chunks"); });
    auto encode_result = msgpack_encode(body, trace_chunks, debug_encode.get());
    if (auto* error = encode_result.if_error()) {
      logger_->log_error(*error);
      return;
    }
  }

  // One HTTP request to the Agent could possibly involve trace chunks from
  // multiple tracers, and thus multiple trace samplers might need to have
  // their rates updated. Unlikely, but possible.
  std::unordered_set<std::shared_ptr<TraceSampler>> response_handlers;
  for (auto& chunk : trace_chunks) {
    response_handlers.insert(std::move(chunk.response_handler));
  }

  // This is the callback for setting request headers.
  // It's invoked synchronously (before `post` returns).
  auto set_request_headers = [&](DictWriter& headers) {
    DebugSpan debug{request_context->debug_request};
    debug.apply([](Span& span) { span.set_name("set_agent_headers"); });
#define SET_HEADER(NAME, VALUE)                          \
  headers.set(NAME, VALUE);                              \
  debug.apply([&](Span& span) {                          \
    span.set_tag("metatrace.agent_header." NAME, VALUE); \
  })
    SET_HEADER("Content-Type", "application/msgpack");
    SET_HEADER("Datadog-Meta-Lang", "cpp");
    SET_HEADER("Datadog-Meta-Lang-Version", std::to_string(__cplusplus));
    SET_HEADER("Datadog-Meta-Tracer-Version", tracer_version);
    SET_HEADER("X-Datadog-Trace-Count", std::to_string(trace_chunks.size()));
#undef SET_HEADER
  };

  // This is the callback for the HTTP response.  It's invoked
  // asynchronously.
  auto on_response = [samplers = std::move(response_handlers), request_context](
                         int response_status,
                         const DictReader& /*response_headers*/,
                         std::string response_body) {
    DebugSpan debug{request_context->debug_request};
    debug.apply([&](Span& span) {
      span.set_name("handle.response");
      span.set_tag("http.response.content_length",
                   std::to_string(response_body.size()));
    });

    if (Optional<Span>& span = request_context->debug_request) {
      span->set_tag("http.status_code", std::to_string(response_status));
      for (TraceChunk& chunk : request_context->chunks) {
        if (Optional<Span>& span = chunk.debug_span) {
          span->set_tag("http.status_code", std::to_string(response_status));
        }
      }
    }

    if (response_status < 200 || response_status >= 300) {
      request_context->logger->log_error([&](auto& stream) {
        stream << "Unexpected response status " << response_status
               << " with body (starts on next line):\n"
               << response_body;
      });
      return;
    }

    auto result = parse_agent_traces_response(response_body);
    if (const auto* error_message = std::get_if<std::string>(&result)) {
      request_context->logger->log_error(*error_message);
      return;
    }
    const auto& response = std::get<CollectorResponse>(result);
    for (const auto& sampler : samplers) {
      if (sampler) {
        sampler->handle_collector_response(response);
      }
    }
  };

  // This is the callback for if something goes wrong sending the
  // request or retrieving the response.  It's invoked
  // asynchronously.
  auto on_error = [request_context](Error error) {
    if (Optional<Span>& span = request_context->debug_request) {
      span->set_error_message(error.message);
      span.reset();  // finish span
      for (TraceChunk& chunk : request_context->chunks) {
        if (Optional<Span>& span = chunk.debug_span) {
          span->set_error_message(error.message);
        }
      }
    }
    request_context->logger->log_error(
        error.with_prefix("Error occurred during HTTP request: "));
  };

  if (debug_span) {
    SpanConfig config;
    config.name = "http.request";
    config.tags["http.method"] = "post";
    // TODO: but Envoy... config.tags["http.url"]
    config.tags["http.url_details.scheme"] = traces_endpoint_.scheme;
    request_context->debug_request.emplace(debug_span->create_child(config));
  }

  auto post_result = http_client_->post(
      traces_endpoint_, std::move(set_request_headers), std::move(body),
      std::move(on_response), std::move(on_error));
  if (auto* error = post_result.if_error()) {
    logger_->log_error(*error);
  }
}

}  // namespace tracing
}  // namespace datadog
