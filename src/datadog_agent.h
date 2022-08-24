#pragma once

#include <memory>
#include <mutex>
#include <vector>

#include "collector.h"
#include "event_scheduler.h"
#include "http_client.h"
#include "validated.h"

namespace datadog {
namespace tracing {

struct DatadogAgentConfig;
struct SpanData;

class DatadogAgent : public Collector {
 public:
  struct TraceChunk {
    std::vector<std::unique_ptr<SpanData>> spans;
    std::function<void(CollectorResponse)> callback;
  };

 private:
  std::mutex mutex_;
  // `incoming_trace_chunks_` are what `send` appends to.
  std::vector<TraceChunk> incoming_trace_chunks_;
  // `outgoing_trace_chunks_` are what `flush` consumes from.
  std::vector<TraceChunk> outgoing_trace_chunks_;
  HTTPClient::URL traces_endpoint_;
  std::shared_ptr<HTTPClient> http_client_;
  std::shared_ptr<EventScheduler> event_scheduler_;
  EventScheduler::Cancel cancel_scheduled_flush_;

  void flush();

 public:
  explicit DatadogAgent(const Validated<DatadogAgentConfig>& config);
  ~DatadogAgent();

  virtual std::optional<Error> send(
      std::vector<std::unique_ptr<SpanData>>&& spans,
      const std::function<void(CollectorResponse)>& callback) override;
};

}  // namespace tracing
}  // namespace datadog
