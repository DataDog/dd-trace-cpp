#include "tracer_telemetry.h"

#include "logger.h"
#include "platform_util.h"
#include "span_defaults.h"
#include "version.h"

namespace datadog {
namespace tracing {

TracerTelemetry::TracerTelemetry(bool enabled, const Clock& clock,
                                 const std::shared_ptr<Logger>& logger,
                                 const TracerSignature& tracer_signature,
                                 const std::string& integration_name,
                                 const std::string& integration_version)
    : enabled_(enabled),
      clock_(clock),
      logger_(logger),
      tracer_signature_(tracer_signature),
      hostname_(get_hostname().value_or("hostname-unavailable")),
      integration_name_(integration_name),
      integration_version_(integration_version) {
  if (enabled_) {
    // Register all the metrics that we're tracking by adding them to the
    // metrics_snapshots_ container. This allows for simpler iteration logic
    // when using the values in `generate-metrics` messages.
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

nlohmann::json TracerTelemetry::generate_telemetry_body(
    std::string request_type) {
  std::time_t tracer_time = std::chrono::duration_cast<std::chrono::seconds>(
                                clock_().wall.time_since_epoch())
                                .count();
  seq_id_++;
  return nlohmann::json::object({
      {"api_version", "v2"},
      {"seq_id", seq_id_},
      {"request_type", request_type},
      {"tracer_time", tracer_time},
      {"runtime_id", tracer_signature_.runtime_id.string()},
      {"debug", debug_},
      {"application",
       nlohmann::json::object({
           {"service_name", tracer_signature_.default_service},
           {"env", tracer_signature_.default_environment},
           {"tracer_version", tracer_signature_.library_version},
           {"language_name", tracer_signature_.library_language},
           {"language_version", tracer_signature_.library_language_version},
       })},
      // TODO: host information (os, os_version, kernel, etc)
      {"host", nlohmann::json::object({
                   {"hostname", hostname_},
               })},
  });
}

std::string TracerTelemetry::app_started() {
  // clang-format off
  auto app_started_msg = nlohmann::json{
    {"request_type", "app-started"},
    {"payload", nlohmann::json{
      {"configuration", nlohmann::json::array()}
    }}
  };

  auto batch = generate_telemetry_body("message-batch");
  batch["payload"] = nlohmann::json::array({
    std::move(app_started_msg)
  });
  // clang-format on

  if (!integration_name_.empty()) {
    // clang-format off
    auto integration_msg = nlohmann::json{
      {"request_type", "app-integrations-change"},
      {"payload", nlohmann::json{
        {"integrations", nlohmann::json::array({
          nlohmann::json{
            {"name", integration_name_},
            {"version", integration_version_},
            {"enabled", true}
          }
        })}
      }}
    };
    // clang-format on

    batch["payload"].emplace_back(std::move(integration_msg));
  }

  return batch.dump();
}

void TracerTelemetry::capture_metrics() {
  std::time_t timepoint = std::chrono::duration_cast<std::chrono::seconds>(
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
      auto type = metric.type();
      if (type == "count") {
        metrics.emplace_back(nlohmann::json::object({
            {"metric", metric.name()},
            {"tags", metric.tags()},
            {"type", metric.type()},
            {"points", points},
            {"common", metric.common()},
        }));
      } else if (type == "gauge") {
        // gauge metrics have a interval
        metrics.emplace_back(nlohmann::json::object({
            {"metric", metric.name()},
            {"tags", metric.tags()},
            {"type", metric.type()},
            {"interval", 10},
            {"points", points},
            {"common", metric.common()},
        }));
      }
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

  auto telemetry_body = generate_telemetry_body("message-batch");
  telemetry_body["payload"] = batch_payloads;
  auto message_batch_payload = telemetry_body.dump();
  return message_batch_payload;
}

std::string TracerTelemetry::app_closing() {
  auto batch_payloads = nlohmann::json::array();

  auto app_closing = nlohmann::json::object({
      {"request_type", "app-closing"},
  });
  batch_payloads.emplace_back(std::move(app_closing));

  auto metrics = nlohmann::json::array();
  for (auto& m : metrics_snapshots_) {
    auto& metric = m.first.get();
    auto& points = m.second;
    if (!points.empty()) {
      auto type = metric.type();
      if (type == "count") {
        metrics.emplace_back(nlohmann::json::object({
            {"metric", metric.name()},
            {"tags", metric.tags()},
            {"type", metric.type()},
            {"points", points},
            {"common", metric.common()},
        }));
      } else if (type == "gauge") {
        // gauge metrics have a interval
        metrics.emplace_back(nlohmann::json::object({
            {"metric", metric.name()},
            {"tags", metric.tags()},
            {"type", metric.type()},
            {"interval", 10},
            {"points", points},
            {"common", metric.common()},
        }));
      }
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

  auto telemetry_body = generate_telemetry_body("message-batch");
  telemetry_body["payload"] = batch_payloads;
  auto message_batch_payload = telemetry_body.dump();
  return message_batch_payload;
}

}  // namespace tracing
}  // namespace datadog
