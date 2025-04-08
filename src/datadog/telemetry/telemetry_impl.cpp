#include "telemetry_impl.h"

#include <datadog/clock.h>
#include <datadog/datadog_agent_config.h>
#include <datadog/dict_writer.h>
#include <datadog/runtime_id.h>
#include <datadog/string_view.h>
#include <datadog/telemetry/telemetry.h>
#include <datadog/tracer_signature.h>

#include <chrono>

#include "datadog_agent.h"
#include "platform_util.h"

using namespace datadog::tracing;
using namespace std::chrono_literals;

namespace datadog::telemetry {
namespace {

HTTPClient::URL make_telemetry_endpoint(HTTPClient::URL url) {
  append(url.path, "/telemetry/proxy/api/v2/apmtelemetry");
  return url;
}

void cancel_tasks(std::vector<tracing::EventScheduler::Cancel>& tasks) {
  for (auto& cancel_task : tasks) {
    cancel_task();
  }
  tasks.clear();
}

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

Telemetry::Telemetry(FinalizedConfiguration config,
                     std::shared_ptr<tracing::Logger> logger,
                     std::shared_ptr<tracing::HTTPClient> client,
                     std::vector<std::shared_ptr<Metric>> user_metrics,
                     std::shared_ptr<tracing::EventScheduler> event_scheduler,
                     HTTPClient::URL agent_url, Clock clock)
    : config_(std::move(config)),
      logger_(std::move(logger)),
      telemetry_endpoint_(make_telemetry_endpoint(agent_url)),
      tracer_signature_(tracing::RuntimeID::generate(),
                        tracing::get_process_name(), ""),
      http_client_(client),
      clock_(std::move(clock)),
      scheduler_(event_scheduler),
      user_metrics_(std::move(user_metrics)),
      host_info_(get_host_info()) {
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

  for (auto& m : user_metrics_) {
    metrics_snapshots_.emplace_back(*m, MetricSnapshot{});
  }

  // Callback for successful telemetry HTTP requests, to examine HTTP
  // status.
  telemetry_on_response_ = [logger = logger_](
                               int response_status,
                               const DictReader& /*response_headers*/,
                               std::string response_body) {
    if (response_status < 200 || response_status >= 300) {
      logger->log_error([&](auto& stream) {
        stream << "Unexpected telemetry response status " << response_status
               << " with body (if any, starts on next line):\n"
               << response_body;
      });
    }
  };

  // Callback for unsuccessful telemetry HTTP requests.
  telemetry_on_error_ = [logger = logger_](Error error) {
    logger->log_error(error.with_prefix(
        "Error occurred during HTTP request for telemetry: "));
  };

  send_telemetry("app-started", app_started());
  schedule_tasks();
}

void Telemetry::schedule_tasks() {
  tasks_.emplace_back(scheduler_->schedule_recurring_event(
      config_.heartbeat_interval, [this]() {
        send_telemetry("app-heartbeat", heartbeat_and_telemetry());
      }));

  if (config_.report_metrics) {
    tasks_.emplace_back(scheduler_->schedule_recurring_event(
        config_.metrics_interval, [this]() mutable { capture_metrics(); }));
  }
}

Telemetry::~Telemetry() {
  if (!tasks_.empty()) {
    cancel_tasks(tasks_);
    capture_metrics();
    // The app-closing message is bundled with a message containing the
    // final metric values.
    auto payload = app_closing();
    send_telemetry("app-closing", payload);
    http_client_->drain(clock_().tick + 2s);
  }
}

Telemetry::Telemetry(Telemetry&& rhs)
    : metrics_(std::move(rhs.metrics_)),
      config_(std::move(rhs.config_)),
      logger_(std::move(rhs.logger_)),
      telemetry_on_response_(std::move(rhs.telemetry_on_response_)),
      telemetry_on_error_(std::move(rhs.telemetry_on_error_)),
      telemetry_endpoint_(std::move(rhs.telemetry_endpoint_)),
      tracer_signature_(std::move(rhs.tracer_signature_)),
      http_client_(rhs.http_client_),
      clock_(std::move(rhs.clock_)),
      scheduler_(std::move(rhs.scheduler_)),
      user_metrics_(std::move(rhs.user_metrics_)),
      seq_id_(rhs.seq_id_),
      config_seq_ids_(rhs.config_seq_ids_),
      host_info_(rhs.host_info_) {
  cancel_tasks(rhs.tasks_);

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

  for (auto& m : user_metrics_) {
    metrics_snapshots_.emplace_back(*m, MetricSnapshot{});
  }

  schedule_tasks();
}

Telemetry& Telemetry::operator=(Telemetry&& rhs) {
  if (&rhs != this) {
    cancel_tasks(rhs.tasks_);

    std::swap(metrics_, rhs.metrics_);
    std::swap(config_, rhs.config_);
    std::swap(logger_, rhs.logger_);
    std::swap(telemetry_on_response_, rhs.telemetry_on_response_);
    std::swap(telemetry_on_error_, rhs.telemetry_on_error_);
    std::swap(telemetry_endpoint_, rhs.telemetry_endpoint_);
    std::swap(http_client_, rhs.http_client_);
    std::swap(tracer_signature_, rhs.tracer_signature_);
    std::swap(http_client_, rhs.http_client_);
    std::swap(clock_, rhs.clock_);
    std::swap(user_metrics_, rhs.user_metrics_);
    std::swap(seq_id_, rhs.seq_id_);
    std::swap(config_seq_ids_, rhs.config_seq_ids_);
    std::swap(host_info_, rhs.host_info_);

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

    for (auto& m : user_metrics_) {
      metrics_snapshots_.emplace_back(*m, MetricSnapshot{});
    }

    schedule_tasks();
  }
  return *this;
}

void Telemetry::log_error(std::string message) {
  if (!config_.report_logs) return;
  log(std::move(message), LogLevel::ERROR);
}

void Telemetry::log_error(std::string message, std::string stacktrace) {
  if (!config_.report_logs) return;
  log(std::move(message), LogLevel::ERROR, stacktrace);
}

void Telemetry::log_warning(std::string message) {
  if (!config_.report_logs) return;
  log(std::move(message), LogLevel::WARNING);
}

void Telemetry::send_telemetry(StringView request_type, std::string payload) {
  auto set_telemetry_headers = [request_type, payload_size = payload.size(),
                                debug_enabled = config_.debug,
                                tracer_signature =
                                    tracer_signature_](DictWriter& headers) {
    /*
      TODO:
        Datadog-Container-ID
    */
    headers.set("Content-Type", "application/json");
    headers.set("Content-Length", std::to_string(payload_size));
    headers.set("DD-Telemetry-API-Version", "v2");
    headers.set("DD-Client-Library-Language", "cpp");
    headers.set("DD-Client-Library-Version", tracer_signature.library_version);
    headers.set("DD-Telemetry-Request-Type", request_type);

    if (debug_enabled) {
      headers.set("DD-Telemetry-Debug-Enabled", "true");
    }
  };

  // auto post_result = http_client_->post(
  //     telemetry_endpoint_, set_telemetry_headers, std::move(payload),
  //     telemetry_on_response_, telemetry_on_error_, clock_().tick + 1s);
  // if (auto* error = post_result.if_error()) {
  //   logger_->log_error(
  //       error->with_prefix("Unexpected error submitting telemetry event: "));
  // }
}

void Telemetry::send_configuration_change() {
  if (configuration_snapshot_.empty()) return;

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

  send_telemetry("app-client-configuration-change", telemetry_body.dump());
}

std::string Telemetry::heartbeat_and_telemetry() {
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

std::string Telemetry::app_closing() {
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

std::string Telemetry::app_started() {
  auto configuration_json = nlohmann::json::array();
  auto product_json = nlohmann::json::object();

  for (const auto& product : config_.products) {
    auto& configurations = product.configurations;
    for (const auto& [_, config_metadata] : configurations) {
      // if (config_metadata.value.empty()) continue;

      configuration_json.emplace_back(
          generate_configuration_field(config_metadata));
    }

    /// NOTE(@dmehala): Telemetry API is tightly related to APM tracing and
    /// assumes telemetry event can only be generated from a tracer. The
    /// assumption is that the tracing product is always enabled and there
    /// is no need to declare it.
    if (product.name == Product::Name::tracing) continue;

    auto p = nlohmann::json{
        {to_string(product.name),
         nlohmann::json{
             {"version", product.version},
             {"enabled", product.enabled},
         }},
    };

    if (product.error_code || product.error_message) {
      auto p_error = nlohmann::json{};
      if (product.error_code) {
        p_error.emplace("code", *product.error_code);
      }
      if (product.error_message) {
        p_error.emplace("message", *product.error_message);
      }

      p.emplace("error", std::move(p_error));
    }

    product_json.emplace(std::move(p));
  }

  auto app_started_msg = nlohmann::json{
      {"request_type", "app-started"},
      {
          "payload",
          nlohmann::json{
              {"configuration", configuration_json},
              {"products", product_json},
          },
      },
  };

  if (config_.install_id || config_.install_time || config_.install_type) {
    auto install_signature = nlohmann::json{};
    if (config_.install_id) {
      install_signature.emplace("install_id", *config_.install_id);
    }
    if (config_.install_type) {
      install_signature.emplace("install_type", *config_.install_type);
    }
    if (config_.install_time) {
      install_signature.emplace("install_time", *config_.install_time);
    }

    app_started_msg["payload"].emplace("install_signature",
                                       std::move(install_signature));
  }

  auto batch = generate_telemetry_body("message-batch");
  batch["payload"] = nlohmann::json::array({std::move(app_started_msg)});

  if (!config_.integration_name.empty()) {
    auto integration_msg = nlohmann::json{
        {"request_type", "app-integrations-change"},
        {
            "payload",
            nlohmann::json{
                {
                    "integrations",
                    nlohmann::json::array({
                        nlohmann::json{{"name", config_.integration_name},
                                       {"version", config_.integration_version},
                                       {"enabled", true}},
                    }),
                },
            },
        },
    };

    batch["payload"].emplace_back(std::move(integration_msg));
  }

  return batch.dump();
}

nlohmann::json Telemetry::generate_telemetry_body(std::string request_type) {
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
      {"debug", config_.debug},
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

nlohmann::json Telemetry::generate_configuration_field(
    const ConfigMetadata& config_metadata) {
  // NOTE(@dmehala): `seq_id` should start at 1 so that the go backend can
  // detect between non set fields.
  config_seq_ids_[config_metadata.name] += 1;
  auto seq_id = config_seq_ids_[config_metadata.name];

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

void Telemetry::capture_configuration_change(
    const std::vector<tracing::ConfigMetadata>& new_configuration) {
  configuration_snapshot_.insert(configuration_snapshot_.begin(),
                                 new_configuration.begin(),
                                 new_configuration.end());
}

void Telemetry::capture_metrics() {
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

void Telemetry::log(std::string message, telemetry::LogLevel level,
                    Optional<std::string> stacktrace) {
  auto timestamp = std::chrono::duration_cast<std::chrono::seconds>(
                       clock_().wall.time_since_epoch())
                       .count();
  logs_.emplace_back(
      telemetry::LogMessage{std::move(message), level, stacktrace, timestamp});
}

}  // namespace datadog::telemetry
