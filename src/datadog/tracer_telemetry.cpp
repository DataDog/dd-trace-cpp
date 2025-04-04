#include "tracer_telemetry.h"

#include <datadog/logger.h>
#include <datadog/span_defaults.h>
#include <datadog/version.h>

#include "platform_util.h"

namespace datadog {
namespace tracing {
namespace {

std::string to_string(datadog::tracing::ConfigName name) {
  switch (name) {
    case ConfigName::SERVICE_NAME:
      return "service";
    case ConfigName::SERVICE_ENV:
      return "env";
    case ConfigName::SERVICE_VERSION:
      return "application_version";
    case ConfigName::REPORT_TRACES:
      return "trace_enabled";
    case ConfigName::TAGS:
      return "trace_tags";
    case ConfigName::EXTRACTION_STYLES:
      return "trace_propagation_style_extract";
    case ConfigName::INJECTION_STYLES:
      return "trace_propagation_style_inject";
    case ConfigName::STARTUP_LOGS:
      return "trace_startup_logs_enabled";
    case ConfigName::REPORT_TELEMETRY:
      return "instrumentation_telemetry_enabled";
    case ConfigName::DELEGATE_SAMPLING:
      return "DD_TRACE_DELEGATE_SAMPLING";
    case ConfigName::GENEREATE_128BIT_TRACE_IDS:
      return "trace_128_bits_id_enabled";
    case ConfigName::AGENT_URL:
      return "trace_agent_url";
    case ConfigName::RC_POLL_INTERVAL:
      return "remote_config_poll_interval";
    case ConfigName::TRACE_SAMPLING_RATE:
      return "trace_sample_rate";
    case ConfigName::TRACE_SAMPLING_LIMIT:
      return "trace_rate_limit";
    case ConfigName::SPAN_SAMPLING_RULES:
      return "span_sample_rules";
    case ConfigName::TRACE_SAMPLING_RULES:
      return "trace_sample_rules";
    case ConfigName::TRACE_BAGGAGE_MAX_BYTES:
      return "trace_baggage_max_bytes";
    case ConfigName::TRACE_BAGGAGE_MAX_ITEMS:
      return "trace_baggage_max_items";
  }

  std::abort();
}

nlohmann::json encode_log(const telemetry::LogMessage& log) {
  auto encoded = nlohmann::json{
      {"message", log.message},
      {"level", to_string(log.level)},
      {"tracer_time", log.timestamp},
  };
  if (log.stacktrace) {
    encoded.emplace("stack_trace", *log.stacktrace);
  }
  return encoded;
}

}  // namespace

TracerTelemetry::TracerTelemetry(
    bool enabled, const Clock& clock, const std::shared_ptr<Logger>& logger,
    const TracerSignature& tracer_signature,
    const std::string& integration_name, const std::string& integration_version,
    const std::vector<std::reference_wrapper<telemetry::Metric>>&
        internal_metrics,
    const std::vector<std::shared_ptr<telemetry::Metric>>& user_metrics)
    : enabled_(enabled),
      clock_(clock),
      logger_(logger),
      host_info_(get_host_info()),
      tracer_signature_(tracer_signature),
      integration_name_(integration_name),
      integration_version_(integration_version),
      user_metrics_(user_metrics) {
  if (enabled_) {
    // Register all the metrics that we're tracking by adding them to the
    // metrics_snapshots_ container. This allows for simpler iteration logic
    // when using the values in `generate-metrics` messages.
    for (auto& m : internal_metrics) {
      metrics_snapshots_.emplace_back(m, MetricSnapshot{});
    }
    for (auto& m : user_metrics_) {
      metrics_snapshots_.emplace_back(*m, MetricSnapshot{});
    }
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
      {"host",
       {
           {"hostname", host_info_.hostname},
           {"os", host_info_.os},
           {"os_version", host_info_.os_version},
           {"architecture", host_info_.cpu_architecture},
           {"kernel_name", host_info_.kernel_name},
           {"kernel_version", host_info_.kernel_version},
           {"kernel_release", host_info_.kernel_release},
       }},
  });
}

nlohmann::json TracerTelemetry::generate_configuration_field(
    const ConfigMetadata& config_metadata) {
  // NOTE(@dmehala): `seq_id` should start at 1 so that the go backend can
  // detect between non set fields.
  config_seq_ids[config_metadata.name] += 1;
  auto seq_id = config_seq_ids[config_metadata.name];

  auto j = nlohmann::json{{"name", to_string(config_metadata.name)},
                          {"value", config_metadata.value},
                          {"seq_id", seq_id}};

  switch (config_metadata.origin) {
    case ConfigMetadata::Origin::ENVIRONMENT_VARIABLE:
      j["origin"] = "env_var";
      break;
    case ConfigMetadata::Origin::CODE:
      j["origin"] = "code";
      break;
    case ConfigMetadata::Origin::REMOTE_CONFIG:
      j["origin"] = "remote_config";
      break;
    case ConfigMetadata::Origin::DEFAULT:
      j["origin"] = "default";
      break;
  }

  if (config_metadata.error) {
    // clang-format off
      j["error"] = {
        {"code", config_metadata.error->code},
        {"message", config_metadata.error->message}
      };
    // clang-format on
  }

  return j;
}

std::string TracerTelemetry::app_started(
    const std::unordered_map<ConfigName, ConfigMetadata>& configurations) {
  auto configuration_json = nlohmann::json::array();
  for (const auto& [_, config_metadata] : configurations) {
    // if (config_metadata.value.empty()) continue;

    configuration_json.emplace_back(
        generate_configuration_field(config_metadata));
  }

  // clang-format off
  auto app_started_msg = nlohmann::json{
    {"request_type", "app-started"},
    {"payload", nlohmann::json{
      {"configuration", configuration_json}
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

void TracerTelemetry::capture_configuration_change(
    const std::vector<ConfigMetadata>& new_configuration) {
  configuration_snapshot_.insert(configuration_snapshot_.begin(),
                                 new_configuration.begin(),
                                 new_configuration.end());
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
            {"namespace", metric.scope()},
            {"common", metric.common()},
        }));
      } else if (type == "gauge") {
        // gauge metrics have a interval
        metrics.emplace_back(nlohmann::json::object({
            {"metric", metric.name()},
            {"tags", metric.tags()},
            {"type", metric.type()},
            {"namespace", metric.scope()},
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
                        {"series", metrics},
                    })},
    });
    batch_payloads.emplace_back(std::move(generate_metrics));
  }

  if (!logs_.empty()) {
    auto encoded_logs = nlohmann::json::array();
    for (const auto& log : logs_) {
      auto encoded = encode_log(log);
      encoded_logs.emplace_back(std::move(encoded));
    }

    assert(!encoded_logs.empty());

    auto logs_payload = nlohmann::json::object({
        {"request_type", "logs"},
        {"payload",
         nlohmann::json{
             {"logs", encoded_logs},
         }},
    });

    batch_payloads.emplace_back(std::move(logs_payload));
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
            {"namespace", metric.scope()},
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
            {"namespace", metric.scope()},
        }));
      }
    }
    points.clear();
  }

  if (!metrics.empty()) {
    auto generate_metrics = nlohmann::json::object({
        {"request_type", "generate-metrics"},
        {"payload", nlohmann::json::object({
                        {"series", metrics},
                    })},
    });
    batch_payloads.emplace_back(std::move(generate_metrics));
  }

  if (!logs_.empty()) {
    auto encoded_logs = nlohmann::json::array();
    for (const auto& log : logs_) {
      auto encoded = encode_log(log);
      encoded_logs.emplace_back(std::move(encoded));
    }

    assert(!encoded_logs.empty());

    auto logs_payload = nlohmann::json::object({
        {"request_type", "logs"},
        {"payload",
         nlohmann::json{
             {"logs", encoded_logs},
         }},
    });

    batch_payloads.emplace_back(std::move(logs_payload));
  }

  auto telemetry_body = generate_telemetry_body("message-batch");
  telemetry_body["payload"] = batch_payloads;
  auto message_batch_payload = telemetry_body.dump();

  return message_batch_payload;
}

Optional<std::string> TracerTelemetry::configuration_change() {
  if (configuration_snapshot_.empty()) return nullopt;

  std::vector<ConfigMetadata> current_configuration;
  std::swap(current_configuration, configuration_snapshot_);

  auto configuration_json = nlohmann::json::array();
  for (const auto& config_metadata : current_configuration) {
    configuration_json.emplace_back(
        generate_configuration_field(config_metadata));
  }

  auto telemetry_body =
      generate_telemetry_body("app-client-configuration-change");
  telemetry_body["payload"] =
      nlohmann::json{{"configuration", configuration_json}};

  return telemetry_body.dump();
}

}  // namespace tracing
}  // namespace datadog
