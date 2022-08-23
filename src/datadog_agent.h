#pragma once

#include <memory>
#include <vector>

#include "collector.h"
#include "event_scheduler.h"
#include "validated.h"

namespace datadog {
namespace tracing {

struct DatadogAgentConfig;
class HTTPClient;
struct SpanData;

class DatadogAgent : public Collector {
  std::vector<std::vector<std::unique_ptr<SpanData>>> trace_chunks_;
  std::shared_ptr<HTTPClient> http_client_;
  std::shared_ptr<EventScheduler> event_scheduler_;
  EventScheduler::Cancel cancel_scheduled_flush_;

 public:
  explicit DatadogAgent(const Validated<DatadogAgentConfig>& config);

  virtual std::optional<Error> send(
      std::vector<std::unique_ptr<SpanData>>&& spans,
      const std::function<void(CollectorResponse)>& callback) override;
};

}  // namespace tracing
}  // namespace datadog
