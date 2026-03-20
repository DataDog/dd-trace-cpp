#include "request_handler.h"

#include <datadog/optional.h>
#include <datadog/sampling_priority.h>
#include <datadog/span_config.h>
#include <datadog/trace_segment.h>
#include <datadog/tracer.h>
#include <datadog/tracer_config.h>

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <datadog/json.hpp>
#include <string>

#include "httplib.h"
#include "utils.h"

RequestHandler::RequestHandler(
    datadog::tracing::FinalizedTracerConfig& tracerConfig,
    std::shared_ptr<ManualScheduler> scheduler,
    std::shared_ptr<DeveloperNoiseLogger> logger)
    : tracer_(tracerConfig),
      scheduler_(scheduler),
      logger_(std::move(logger)),
      local_stable_config_values_(tracerConfig.local_stable_config_values),
      fleet_stable_config_values_(tracerConfig.fleet_stable_config_values) {}

void RequestHandler::set_error(const char* const file, int line,
                               const std::string& reason,
                               httplib::Response& res) {
  logger_->log_info(reason);

  // clang-format off
    const auto error = nlohmann::json{
        {"detail", {
          {"loc", nlohmann::json::array({file, line})},
          {"msg", reason},
          {"type", "Validation Error"}
        }}
    };
  // clang-format on

  res.status = 422;
  res.set_content(error.dump(), "application/json");
}

#define VALIDATION_ERROR(res, msg)         \
  set_error(__FILE__, __LINE__, msg, res); \
  return

void RequestHandler::on_trace_config(const httplib::Request& /* req */,
                                     httplib::Response& res) {
  auto tracer_cfg = nlohmann::json::parse(tracer_.config());

  // Helper: convert a DD_* key name to lowercase (e.g. "DD_SERVICE" ->
  // "dd_service").
  auto to_lower_key = [](const std::string& key) -> std::string {
    std::string result = key;
    std::transform(result.begin(), result.end(), result.begin(),
                   [](unsigned char c) -> char {
                     return static_cast<char>(std::tolower(c));
                   });
    return result;
  };

  // clang-format off
    auto response_body = nlohmann::json{
      { "config", {
          { "dd_service", tracer_cfg["defaults"]["service"]},
          { "dd_env", tracer_cfg["defaults"]["environment"]},
          { "dd_version", tracer_cfg["environment_variables"]["version"]},
          { "dd_trace_enabled", tracer_cfg["environment_variables"]["report_traces"]},
          { "dd_trace_agent_url", tracer_cfg["environment_variables"]["DD_TRACE_AGENT_URL"]}
        }
      }
    };
  // clang-format on

  if (tracer_cfg.contains("trace_sampler")) {
    auto trace_sampler_cfg = tracer_cfg["trace_sampler"];
    if (trace_sampler_cfg.contains("max_per_second")) {
      response_body["config"]["dd_trace_rate_limit"] =
          std::to_string((int)trace_sampler_cfg["max_per_second"]);
    }
  }

  // Helper: normalize boolean-like strings to lowercase.
  auto normalize_value = [](const std::string& val) -> std::string {
    std::string lower = val;
    std::transform(lower.begin(), lower.end(), lower.begin(),
                   [](unsigned char c) -> char {
                     return static_cast<char>(std::tolower(c));
                   });
    if (lower == "true" || lower == "false") {
      return lower;
    }
    return val;
  };

  // Merge stable config and env var values with correct precedence.
  // Precedence: fleet_stable > env > local_stable
  std::unordered_map<std::string, std::string> effective_config;

  // 1. Local stable config (lowest precedence)
  for (const auto& [key, value] : local_stable_config_values_) {
    effective_config[key] = normalize_value(value);
  }

  // 3. Environment variables (for keys we're tracking)
  for (const auto& [key, value] : effective_config) {
    const char* env_val = std::getenv(key.c_str());
    if (env_val != nullptr) {
      effective_config[key] = normalize_value(std::string(env_val));
    }
  }

  // 4. Fleet stable config (highest precedence)
  for (const auto& [key, value] : fleet_stable_config_values_) {
    effective_config[key] = normalize_value(value);
  }

  // Write all effective config values to the response.
  for (const auto& [key, value] : effective_config) {
    auto lower_key = to_lower_key(key);
    // Only set if not already present from the native config above.
    if (!response_body["config"].contains(lower_key)) {
      response_body["config"][lower_key] = value;
    }
  }

  logger_->log_info(response_body.dump());
  res.set_content(response_body.dump(), "application/json");
}

// TODO: Refact endpoint handler to return 404 when an unknown field is passed
// in the payload. that would send a clear message instead of silently creating
// a span.
void RequestHandler::on_span_start(const httplib::Request& req,
                                   httplib::Response& res) {
  const auto request_json = nlohmann::json::parse(req.body);

  datadog::tracing::SpanConfig span_cfg;
  span_cfg.name = request_json.at("name");

  if (auto service =
          utils::get_if_exists<std::string_view>(request_json, "service")) {
    if (!service->empty()) {
      span_cfg.service = *service;
    }
  }

  if (auto service_type =
          utils::get_if_exists<std::string_view>(request_json, "type")) {
    span_cfg.service_type = *service_type;
  }

  if (auto resource =
          utils::get_if_exists<std::string_view>(request_json, "resource")) {
    span_cfg.resource = *resource;
  }

  if (auto tags = request_json.find("span_tags");
      tags != request_json.cend() && tags->is_array()) {
    for (const auto& tag : *tags) {
      if (tag.size() != 2) {
        // TODO: refactor to log errors
        continue;
      }

      if (tag[0].is_string() == false || tag[1].is_string() == false) {
        // TODO: refactor to log errors
        continue;
      }

      span_cfg.tags.emplace(tag[0], tag[1]);
    }
  }

  auto success = [](const datadog::tracing::Span& span,
                    httplib::Response& res) {
    // clang-format off
      const auto response_body = nlohmann::json{
        { "trace_id", span.trace_id().low },
        { "span_id", span.id() }
      };
    // clang-format on

    res.set_content(response_body.dump(), "application/json");
  };

  auto parent_id = utils::get_if_exists<uint64_t>(request_json, "parent_id");

  // No `parent_id` field OR parent is `0` -> create a span.
  if (!parent_id || *parent_id == 0) {
    auto span = tracer_.create_span(span_cfg);
    success(span, res);
    active_spans_.emplace(span.id(), std::move(span));
    return;
  }

  // If there's a parent ID -> Extract using the tracing context stored earlier
  //                        OR -> Create a child span from the span.
  auto parent_span_it = active_spans_.find(*parent_id);
  if (parent_span_it != active_spans_.cend()) {
    auto span = parent_span_it->second.create_child(span_cfg);
    success(span, res);
    active_spans_.emplace(span.id(), std::move(span));
    return;
  }

  auto context_it = tracing_context_.find(*parent_id);
  if (context_it != tracing_context_.cend()) {
    auto span =
        tracer_.extract_span(utils::HeaderReader(context_it->second), span_cfg);
    if (!span) {
      const auto msg =
          "on_span_start: unable to create span from http_headers "
          "identified "
          "by parent_id " +
          std::to_string(*parent_id);
      VALIDATION_ERROR(res, msg);
      return;
    }
    success(*span, res);
    active_spans_.emplace(span->id(), std::move(*span));
    return;
  }

  // Safeguard
  if (*parent_id != 0) {
    const auto msg = "on_span_start: span or http_headers not found for id " +
                     std::to_string(*parent_id);
    VALIDATION_ERROR(res, msg);
  }
}

void RequestHandler::on_span_end(const httplib::Request& req,
                                 httplib::Response& res) {
  const auto now = std::chrono::steady_clock::now();
  const auto request_json = nlohmann::json::parse(req.body);

  auto span_id = utils::get_if_exists<uint64_t>(request_json, "span_id");
  if (!span_id) {
    VALIDATION_ERROR(res, "on_span_end: missing `span_id` field.");
  }

  auto span_it = active_spans_.find(*span_id);
  if (span_it == active_spans_.cend()) {
    const auto msg =
        "on_span_end: span not found for id " + std::to_string(*span_id);
    VALIDATION_ERROR(res, msg);
  }

  span_it->second.set_end_time(now);
  res.status = 200;
}

void RequestHandler::on_set_meta(const httplib::Request& req,
                                 httplib::Response& res) {
  const auto request_json = nlohmann::json::parse(req.body);

  auto span_id = utils::get_if_exists<uint64_t>(request_json, "span_id");
  if (!span_id) {
    VALIDATION_ERROR(res, "on_set_meta: missing `span_id` field.");
  }

  auto span_it = active_spans_.find(*span_id);
  if (span_it == active_spans_.cend()) {
    const auto msg =
        "on_set_meta: span not found for id " + std::to_string(*span_id);
    VALIDATION_ERROR(res, msg);
  }

  auto& span = span_it->second;
  span.set_tag(request_json.at("key").get<std::string_view>(),
               request_json.at("value").get<std::string_view>());

  res.status = 200;
}

void RequestHandler::on_set_metric(const httplib::Request& req,
                                   httplib::Response& res) {
  const auto request_json = nlohmann::json::parse(req.body);

  auto span_id = utils::get_if_exists<uint64_t>(request_json, "span_id");
  if (!span_id) {
    VALIDATION_ERROR(res, "on_set_metric: missing `span_id` field.");
  }

  auto span_it = active_spans_.find(*span_id);
  if (span_it == active_spans_.cend()) {
    const auto msg =
        "on_set_metric: span not found for id " + std::to_string(*span_id);
    VALIDATION_ERROR(res, msg);
  }

  auto& span = span_it->second;
  span.set_metric(request_json.at("key").get<std::string_view>(),
                  request_json.at("value").get<double>());

  res.status = 200;
}

void RequestHandler::on_manual_keep(const httplib::Request& req,
                                    httplib::Response& res) {
  const auto request_json = nlohmann::json::parse(req.body);

  auto span_id = utils::get_if_exists<uint64_t>(request_json, "span_id");
  if (!span_id) {
    VALIDATION_ERROR(res, "on_manual_keep: missing `span_id` field.");
  }

  auto span_it = active_spans_.find(*span_id);
  if (span_it == active_spans_.cend()) {
    const auto msg =
        "on_manual_keep: span not found for id " + std::to_string(*span_id);
    VALIDATION_ERROR(res, msg);
  }

  auto& span = span_it->second;
  span.trace_segment().override_sampling_priority(
      datadog::tracing::SamplingPriority::USER_KEEP);

  res.status = 200;
}

void RequestHandler::on_manual_drop(const httplib::Request& req,
                                    httplib::Response& res) {
  const auto request_json = nlohmann::json::parse(req.body);

  auto span_id = utils::get_if_exists<uint64_t>(request_json, "span_id");
  if (!span_id) {
    VALIDATION_ERROR(res, "on_manual_drop: missing `span_id` field.");
  }

  auto span_it = active_spans_.find(*span_id);
  if (span_it == active_spans_.cend()) {
    const auto msg =
        "on_manual_drop: span not found for id " + std::to_string(*span_id);
    VALIDATION_ERROR(res, msg);
  }

  auto& span = span_it->second;
  span.trace_segment().override_sampling_priority(
      datadog::tracing::SamplingPriority::USER_DROP);

  res.status = 200;
}

void RequestHandler::on_inject_headers(const httplib::Request& req,
                                       httplib::Response& res) {
  const auto request_json = nlohmann::json::parse(req.body);

  auto span_id = utils::get_if_exists<uint64_t>(request_json, "span_id");
  if (!span_id) {
    VALIDATION_ERROR(res, "on_inject_headers: missing `span_id` field.");
  }

  auto span_it = active_spans_.find(*span_id);
  if (span_it == active_spans_.cend()) {
    const auto msg =
        "on_inject_headers: span not found for id " + std::to_string(*span_id);
    VALIDATION_ERROR(res, msg);
  }

  // clang-format off
    nlohmann::json response_json{
      { "http_headers", nlohmann::json::array() }
    };
  // clang-format on

  utils::HeaderWriter writer(response_json["http_headers"]);
  span_it->second.inject(writer);

  res.set_content(response_json.dump(), "application/json");
}

void RequestHandler::on_extract_headers(const httplib::Request& req,
                                        httplib::Response& res) {
  const auto request_json = nlohmann::json::parse(req.body);
  auto http_headers = utils::get_if_exists<nlohmann::json::array_t>(
      request_json, "http_headers");
  if (!http_headers) {
    VALIDATION_ERROR(res, "on_extract_headers: missing `http_headers` field.");
  }

  auto span = tracer_.extract_span(utils::HeaderReader(*http_headers));

  if (span.if_error()) {
    const auto response_body_fail = nlohmann::json{
        {"span_id", nullptr},
    };
    res.set_content(response_body_fail.dump(), "application/json");
    return;
  }

  const auto response_body = nlohmann::json{
      {"span_id", span->parent_id().value()},
  };

  tracing_context_[*span->parent_id()] = std::move(*http_headers);

  // The span below will not be finished and flushed.
  blackhole_.emplace_back(std::move(*span));

  res.set_content(response_body.dump(), "application/json");
}

void RequestHandler::on_span_flush(const httplib::Request& /* req */,
                                   httplib::Response& res) {
  scheduler_->flush_telemetry();
  active_spans_.clear();
  tracing_context_.clear();
  res.status = 200;
}

void RequestHandler::on_stats_flush(const httplib::Request& /* req */,
                                    httplib::Response& res) {
  scheduler_->flush_traces();
  res.status = 200;
}

void RequestHandler::on_span_error(const httplib::Request& req,
                                   httplib::Response& res) {
  const auto request_json = nlohmann::json::parse(req.body);

  auto span_id = utils::get_if_exists<uint64_t>(request_json, "span_id");
  if (!span_id) {
    VALIDATION_ERROR(res, "on_span_error: missing `span_id` field.");
  }

  auto span_it = active_spans_.find(*span_id);
  if (span_it == active_spans_.cend()) {
    const auto msg =
        "on_span_error: span not found for id " + std::to_string(*span_id);
    VALIDATION_ERROR(res, msg);
  }

  auto& span = span_it->second;

  if (auto type =
          utils::get_if_exists<std::string_view>(request_json, "type")) {
    if (!type->empty()) span.set_error_type(*type);
  }

  if (auto message =
          utils::get_if_exists<std::string_view>(request_json, "message")) {
    if (!message->empty()) span.set_error_message(*message);
  }

  if (auto stack =
          utils::get_if_exists<std::string_view>(request_json, "stack")) {
    if (!stack->empty()) span.set_error_stack(*stack);
  }

  res.status = 200;
}

#undef VALIDATION_ERROR
