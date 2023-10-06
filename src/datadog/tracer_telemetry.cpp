#include "tracer_telemetry.h"

#include "json.hpp"
#include "logger.h"
#include "platform_util.h"
#include "span_defaults.h"
#include "version.h"

namespace datadog {
namespace tracing {

TracerTelemetry::TracerTelemetry(
    bool enabled, const Clock& clock, const std::shared_ptr<Logger>& logger,
    const std::shared_ptr<const SpanDefaults>& span_defaults)
    : enabled_(enabled),
      clock_(clock),
      logger_(logger),
      span_defaults_(span_defaults),
      hostname_(get_hostname().value_or("hostname-unavailable")) {
  if (enabled_) {
    metrics_snapshots_.emplace_back(metrics_.tracer.spans_created,
                                    MetricSnapshot{});
    metrics_snapshots_.emplace_back(metrics_.tracer.spans_finished,
                                    MetricSnapshot{});
    metrics_snapshots_.emplace_back(metrics_.tracer.trace_segments_created_new,
                                    MetricSnapshot{});
    metrics_snapshots_.emplace_back(
        metrics_.tracer.trace_segments_created_continued, MetricSnapshot{});
    metrics_snapshots_.emplace_back(metrics_.tracer.trace_segments_closed,
                                    MetricSnapshot{});
    metrics_snapshots_.emplace_back(metrics_.trace_api.requests,
                                    MetricSnapshot{});
    metrics_snapshots_.emplace_back(metrics_.trace_api.responses_1xx,
                                    MetricSnapshot{});
    metrics_snapshots_.emplace_back(metrics_.trace_api.responses_2xx,
                                    MetricSnapshot{});
    metrics_snapshots_.emplace_back(metrics_.trace_api.responses_3xx,
                                    MetricSnapshot{});
    metrics_snapshots_.emplace_back(metrics_.trace_api.responses_4xx,
                                    MetricSnapshot{});
    metrics_snapshots_.emplace_back(metrics_.trace_api.responses_5xx,
                                    MetricSnapshot{});
    metrics_snapshots_.emplace_back(metrics_.trace_api.errors_timeout,
                                    MetricSnapshot{});
    metrics_snapshots_.emplace_back(metrics_.trace_api.errors_network,
                                    MetricSnapshot{});
    metrics_snapshots_.emplace_back(metrics_.trace_api.errors_status_code,
                                    MetricSnapshot{});
  }
}

std::string TracerTelemetry::app_started(nlohmann::json&& tracer_config) {
  time_t tracer_time = std::chrono::duration_cast<std::chrono::seconds>(
                           clock_().wall.time_since_epoch())
                           .count();

  seq_id++;
  auto payload =
      nlohmann::json::object(
          {
              {"api_version", "v2"},
              {"seq_id", seq_id},
              {"request_type", "app-started"},
              {"tracer_time", tracer_time},
              {"runtime_id", span_defaults_->runtime_id},
              {"debug", debug_},
              {"application",
               nlohmann::json::object({
                   {"service_name", span_defaults_->service},
                   {"env", span_defaults_->environment},
                   {"tracer_version", tracer_version_string},
                   {"language_name", "cpp"},
                   {"language_version", std::to_string(__cplusplus)},
               })},
              // TODO: host information (os, os_version, kernel, etc)
              {"host", nlohmann::json::object({
                           {"hostname", hostname_},
                       })},
              {"payload",
               nlohmann::json::object({
                   {"configuration", nlohmann::json::array({
                                         // TODO: environment variables or
                                         // finalized config details
                                     })},

               })},
              // TODO: Until we figure out "configuration", above, include a
              // JSON dump of the tracer configuration as "additional_payload".
              {"additional_payload",
               nlohmann::json::array({nlohmann::json::object({
                   {"name", "tracer_config_json"},
                   {"value", tracer_config.dump()},
               })})},
          })
          .dump();

  return payload;
}

void TracerTelemetry::capture_metrics() {
  time_t timepoint = std::chrono::duration_cast<std::chrono::seconds>(
                         clock_().wall.time_since_epoch())
                         .count();
  for (auto& m : metrics_snapshots_) {
    auto value = m.first.get().capture_and_reset_value();
    if (value == 0) {
      continue;
    }
    m.second.emplace_back(timepoint, value);
  }
}

std::string TracerTelemetry::heartbeat_and_telemetry() {
  time_t tracer_time = std::chrono::duration_cast<std::chrono::seconds>(
                           clock_().wall.time_since_epoch())
                           .count();
  auto batch_payloads = nlohmann::json::array();

  auto heartbeat = nlohmann::json::object({
      {"request_type", "app-heartbeat"},
  });
  batch_payloads.emplace_back(std::move(heartbeat));

  auto metrics = nlohmann::json::array();
  for (auto& m : metrics_snapshots_) {
    auto& metric = m.first.get();
    auto& points = m.second;
    if (!points.empty()) {
      metrics.emplace_back(nlohmann::json::object({
          {"metric", metric.name()},
          {"tags", metric.tags()},
          {"type", metric.type()},
          {"interval", 60},
          {"points", points},
          {"common", metric.common()},
      }));
    }
    points.clear();
  }

  if (!metrics.empty()) {
    auto generate_metrics = nlohmann::json::object({
        {"request_type", "generate-metrics"},
        {"payload", nlohmann::json::object({
                        {"namespace", "tracers"},
                        {"series", metrics},
                    })},
    });
    batch_payloads.emplace_back(std::move(generate_metrics));
  }

  seq_id++;
  auto payload =
      nlohmann::json::object(
          {
              {"api_version", "v2"},
              {"seq_id", seq_id},
              {"request_type", "message-batch"},
              {"tracer_time", tracer_time},
              {"runtime_id", span_defaults_->runtime_id},
              {"debug", debug_},
              {"application",
               nlohmann::json::object({
                   {"service_name", span_defaults_->service},
                   {"env", span_defaults_->environment},
                   {"tracer_version", tracer_version_string},
                   {"language_name", "cpp"},
                   {"language_version", std::to_string(__cplusplus)},
               })},
              // TODO: host information (hostname, os, os_version, kernel, etc)
              {"host", nlohmann::json::object({
                           {"hostname", hostname_},
                       })},
              {"payload", batch_payloads},
          })
          .dump();
  return payload;
}

}  // namespace tracing
}  // namespace datadog
