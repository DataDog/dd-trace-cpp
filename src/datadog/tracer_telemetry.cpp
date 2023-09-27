#include "tracer_telemetry.h"

#include <iostream>

#include "json.hpp"
#include "logger.h"
#include "platform_util.h"
#include "version.h"

namespace datadog {
namespace tracing {

TracerTelemetry::TracerTelemetry(const Clock& clock,
                                 const FinalizedTracerConfig& config)
    : clock_(clock), config_(config) {
      /*
      metrics_.emplace_back(traces_started_, MetricSnapshot{});
      metrics_.emplace_back(traces_finished_, MetricSnapshot{});
      metrics_.emplace_back(active_traces_, MetricSnapshot{});
      */
      metrics_.emplace_back(trace_api_requests_, MetricSnapshot{});
    }


std::string TracerTelemetry::appStarted() {
  time_t tracer_time = std::chrono::duration_cast<std::chrono::seconds>(
                                  clock_().wall.time_since_epoch())
                                  .count();
  std::string hostname = get_hostname().value_or("hostname-unavailable");
  config_.logger->log_error([&](auto& stream) {
      stream << "app-started: hostname=" << hostname << " seq_id=" << seq_id;
      });
  
  seq_id++;
  auto payload =
      nlohmann::json::object({
              {"api_version", "v2"},
              {"seq_id", seq_id},
              {"request_type", "app-started"},
              {"tracer_time", tracer_time},
              {"runtime_id", "0524398a-11e2-4375-a637-619eb9148e8f"},
              {"debug", true},
              {"application",
               nlohmann::json::object({
                   {"service_name", config_.defaults.service},
                   {"env", config_.defaults.environment},
                   {"tracer_version", tracer_version_string},
                   {"language_name", "cpp"},
                   {"language_version", std::to_string(__cplusplus)},
              })},
              // TODO: host information (hostname, os, os_version, kernel, etc)
              {"host", nlohmann::json::object({
                   {"hostname", hostname},
              })},
              {"payload",
               nlohmann::json::object({
                   {"configuration", nlohmann::json::array({
                                         // TODO: environment variables or
                                         // finalized config details
                                     })},

              })},
          })
          .dump();

  return payload;
}

void TracerTelemetry::captureMetrics() {
  time_t timepoint = std::chrono::duration_cast<std::chrono::seconds>(
                                  clock_().wall.time_since_epoch())
                                  .count();
  for (auto& m : metrics_) {
    m.second.emplace_back(timepoint, m.first.get().value());
  }

  for (auto& m : metrics_) {
    std::cout << "metrics: " << m.first.get().name() << std::endl;
    for (auto& v : m.second) {
      std::cout << v.first << " " << v.second << std::endl;
    }
  }
}

std::string TracerTelemetry::heartbeatAndTelemetry() {
  time_t tracer_time = std::chrono::duration_cast<std::chrono::seconds>(
                                  clock_().wall.time_since_epoch())
                                  .count();
  std::string hostname = get_hostname().value_or("hostname-unavailable");

  auto heartbeat = nlohmann::json::object({
      {"request_type", "app-heartbeat"},
      });

  auto metrics = nlohmann::json::array();
  for (auto& m : metrics_) {
    auto& metric = m.first.get();
    auto& points = m.second;
    metrics.emplace_back(nlohmann::json::object({
          {"metric", metric.name()},
          {"type", metric.type()},
          {"interval", 60},
          {"points", points},
          {"common", metric.common()},
          }));
    m.second.clear();
  }

  auto generate_metrics = nlohmann::json::object({
      {"request_type", "generate-metrics"},
      {"payload", nlohmann::json::object({
          {"namespace", "tracers"},
          {"series", metrics},
          })},
      });
  
  seq_id++;
  auto payload = nlohmann::json::object({
      {"api_version", "v2"},
      {"seq_id", seq_id},
      {"request_type", "message-batch"},
      {"tracer_time", tracer_time},
      {"runtime_id", "0524398a-11e2-4375-a637-619eb9148e8f"},
      {"debug", true},
      {"application",
      nlohmann::json::object({
          {"service_name", config_.defaults.service},
          {"env", config_.defaults.environment},
          {"tracer_version", tracer_version_string},
          {"language_name", "cpp"},
          {"language_version", std::to_string(__cplusplus)},
          })},
      // TODO: host information (hostname, os, os_version, kernel, etc)
      {"host", nlohmann::json::object({
          {"hostname", hostname},
          })},
      {"payload", nlohmann::json::array({
          heartbeat,
          generate_metrics,
          })},
  }).dump();
  config_.logger->log_error([&](auto& stream) {
      stream << "telemetry payload: " << payload;
      });
  return payload;
}

}  // namespace tracing
}  // namespace datadog
