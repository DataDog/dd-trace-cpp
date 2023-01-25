#pragma once

// This component provides a `class`, `DatadogAgent`, that implements the
// `Collector` interface in terms of periodic HTTP requests to a Datadog Agent.
//
// `DatadogAgent` is configured by `DatadogAgentConfig`.  See
// `datadog_agent_config.h`.

#include <memory>
#include <mutex>
#include <vector>

#include "clock.h"
#include "collector.h"
#include "event_scheduler.h"
#include "http_client.h"

namespace datadog {
namespace tracing {

class FinalizedDatadogAgentConfig;
class Logger;
struct SpanData;
class TraceSampler;

class DatadogAgent : public Collector {
 public:
  struct TraceChunk {
    std::vector<std::unique_ptr<SpanData>> spans;
    std::shared_ptr<TraceSampler> response_handler;
  };

 private:
  std::mutex mutex_;
  Clock clock_;
  std::shared_ptr<Logger> logger_;
  std::vector<TraceChunk> trace_chunks_;
  HTTPClient::URL traces_endpoint_;
  std::shared_ptr<HTTPClient> http_client_;
  std::shared_ptr<EventScheduler> event_scheduler_;
  EventScheduler::Cancel cancel_scheduled_flush_;
  std::chrono::steady_clock::duration flush_interval_;

  void flush();

 public:
  DatadogAgent(const FinalizedDatadogAgentConfig&, const Clock& clock,
               const std::shared_ptr<Logger>&);
  ~DatadogAgent();

  Expected<void> send(
      std::vector<std::unique_ptr<SpanData>>&& spans,
      const std::shared_ptr<TraceSampler>& response_handler) override;

  nlohmann::json config_json() const override;
};

}  // namespace tracing
}  // namespace datadog
