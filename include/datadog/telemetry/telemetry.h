#pragma once

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
  std::shared_ptr<tracing::Logger> logger_;

  std::shared_ptr<tracing::DatadogAgent> datadog_agent_;
  std::shared_ptr<tracing::TracerTelemetry> tracer_telemetry_;

 public:
  Telemetry(FinalizedConfiguration configuration,
            std::shared_ptr<tracing::Logger> logger,
            std::vector<std::shared_ptr<Metric>> metrics);

  ~Telemetry() = default;

  void log_error(std::string message);
  void log_warning(std::string message);
};

}  // namespace telemetry
}  // namespace datadog
