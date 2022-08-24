#include "datadog_agent.h"

#include <cassert>
#include <cctype>    // TODO: no
#include <iomanip>   // TODO: no
#include <iostream>  // TODO: no

#include "collector_response.h"
#include "datadog_agent_config.h"
#include "dict_writer.h"
#include "msgpackpp.h"
#include "span_data.h"

namespace datadog {
namespace tracing {
namespace {

const std::string_view traces_api_path = "/v0.4/traces";

HTTPClient::URL traces_endpoint(std::string_view agent_url) {
  // `agent_url` came from a validated configuration, so it is guaranteed to
  // parse successfully.
  auto url = std::get<HTTPClient::URL>(DatadogAgentConfig::parse(agent_url));
  url.path += traces_api_path;
  return url;
}

std::optional<Error> msgpack_encode(
    std::string& destination,
    const std::vector<std::unique_ptr<SpanData>>& spans) {
  {
    msgpackpp::packer packer{&destination};
    packer.pack_array(spans.size());
  }

  for (const auto& span_ptr : spans) {
    assert(span_ptr);
    if (auto maybe_error = msgpack_encode(destination, *span_ptr)) {
      return maybe_error;
    }
  }

  return std::nullopt;
}

std::optional<Error> msgpack_encode(
    std::string& destination,
    const std::vector<DatadogAgent::TraceChunk>& trace_chunks) {
  {
    msgpackpp::packer packer{&destination};
    packer.pack_array(trace_chunks.size());
  }

  for (const auto& chunk : trace_chunks) {
    if (auto maybe_error = msgpack_encode(destination, chunk.spans)) {
      return maybe_error;
    }
  }

  return std::nullopt;
}

}  // namespace

DatadogAgent::DatadogAgent(const Validated<DatadogAgentConfig>& config)
    : traces_endpoint_(traces_endpoint(config.agent_url)),
      http_client_(config.http_client),
      event_scheduler_(config.event_scheduler),
      cancel_scheduled_flush_(event_scheduler_->schedule_recurring_event(
          std::chrono::milliseconds(config.flush_interval_milliseconds),
          [this]() { flush(); })) {}

DatadogAgent::~DatadogAgent() { cancel_scheduled_flush_(); }

std::optional<Error> DatadogAgent::send(
    std::vector<std::unique_ptr<SpanData>>&& spans,
    const std::function<void(CollectorResponse)>& on_response) {
  std::lock_guard<std::mutex> lock(mutex_);
  incoming_trace_chunks_.push_back(TraceChunk{std::move(spans), on_response});
  return std::nullopt;
}

void DatadogAgent::flush() {
  outgoing_trace_chunks_.clear();
  {
    std::lock_guard<std::mutex> lock(mutex_);
    using std::swap;
    swap(incoming_trace_chunks_, outgoing_trace_chunks_);
  }

  // For X-Datadog-Trace-Count, below.
  const std::string trace_count = std::to_string(outgoing_trace_chunks_.size());

  for (auto& chunk : outgoing_trace_chunks_) {
    std::string body;
    if (const auto maybe_error = msgpack_encode(body, outgoing_trace_chunks_)) {
      // TODO: need a logger
      (void)maybe_error;
      continue;
    }

    if (const auto maybe_error = http_client_->post(
            traces_endpoint_,
            // This is the callback for setting request headers.
            // This is invoked immediately before `post` returns.
            [&trace_count](DictWriter& headers) {
              headers.set("Content-Type", "application/msgpack");
              headers.set("Datadog-Meta-Lang", "cpp");
              headers.set("Datadog-Meta-Lang-Version",
                          std::to_string(__cplusplus));
              headers.set("Datadog-Meta-Tracer-Version", "TODO");
              headers.set("X-Datadog-Trace-Count", trace_count);
            },
            // This is the request body.
            std::move(body),
            // This is the callback for the HTTP response.  It's invoked
            // asynchronously.
            [callback = std::move(chunk.callback)](
                int response_status, const DictReader& /*response_headers*/,
                std::string response_body) {
              // TODO: response handler
              std::cout << "Received HTTP response status " << std::dec
                        << response_status << '\n';
              std::cout << "Received HTTP response body (begins on following "
                           "line):\n";
              for (const unsigned char byte : response_body) {
                if (std::isprint(byte)) {
                  std::cout << static_cast<char>(byte);
                } else {
                  std::cout << "\\x" << std::hex << std::setfill('0')
                            << std::setw(2) << static_cast<unsigned>(byte);
                }
              }
              std::cout << '\n';
            },
            // This is the callback for if something goes wrong sending the
            // request or retrieving the response.  It's invoked
            // asynchronously.
            [](Error error) {
              // TODO: error handler
              std::cout << "Error occurred during HTTP request: " << error
                        << '\n';
            })) {
      // TODO: need logger
      (void)maybe_error;
    }
  }
}

}  // namespace tracing
}  // namespace datadog
