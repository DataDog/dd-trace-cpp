#pragma once

#include <datadog/event_scheduler.h>
#include <datadog/http_client.h>
#include <datadog/logger.h>
#include <datadog/telemetry/configuration.h>
#include <datadog/telemetry/metrics.h>

#include <memory>
#include <vector>

namespace datadog {

namespace tracing {
class DatadogAgent;
class TracerTelemetry;
}  // namespace tracing

namespace telemetry {

class Telemetry {
  FinalizedConfiguration config_;
  std::shared_ptr<tracing::HTTPClient> http_client_;
  std::shared_ptr<tracing::Logger> logger_;

  std::shared_ptr<tracing::DatadogAgent> datadog_agent_;
  std::shared_ptr<tracing::TracerTelemetry> tracer_telemetry_;

 public:
  Telemetry(FinalizedConfiguration configuration,
            std::shared_ptr<tracing::EventScheduler> scheduler,
            std::shared_ptr<tracing::HTTPClient> http_client,
            std::shared_ptr<tracing::Logger> logger,
            std::vector<std::shared_ptr<Metric>> metrics);

  ~Telemetry() = default;
};

}  // namespace telemetry
}  // namespace datadog
