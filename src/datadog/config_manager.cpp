#include "config_manager.h"

#include <sstream>

#include "parse_util.h"
#include "trace_sampler.h"

namespace datadog {
namespace tracing {
namespace {

std::string join(const std::vector<StringView>& values,
                 const char* const separator) {
  if (values.empty()) return {};
  auto it = values.cbegin();

  std::string res{*it};
  for (++it; it != values.cend(); ++it) {
    res += separator;
    append(res, *it);
  }

  return res;
}

// TODO: use `to_chars`
std::string to_string(double d) {
  std::stringstream stream;
  stream << std::fixed << std::setprecision(1) << d;
  return stream.str();
}

}  // namespace

ConfigManager::ConfigManager(const FinalizedTracerConfig& config)
    : clock_(config.clock),
      default_config_metadata_(config.metadata),
      trace_sampler_(
          std::make_shared<TraceSampler>(config.trace_sampler, clock_)),
      span_defaults_(std::make_shared<SpanDefaults>(config.defaults)),
      report_traces_(config.report_traces) {}

std::shared_ptr<TraceSampler> ConfigManager::trace_sampler() {
  std::lock_guard<std::mutex> lock(mutex_);
  return trace_sampler_;
}

std::shared_ptr<const SpanDefaults> ConfigManager::span_defaults() {
  std::lock_guard<std::mutex> lock(mutex_);
  return span_defaults_;
}

bool ConfigManager::report_traces() {
  std::lock_guard<std::mutex> lock(mutex_);
  return report_traces_;
}

std::vector<ConfigMetadata> ConfigManager::update(const ConfigUpdate& conf) {
  std::vector<ConfigMetadata> telemetry_conf;

  std::lock_guard<std::mutex> lock(mutex_);

  if (conf.trace_sampling_rate) {
    ConfigMetadata trace_sampling_metadata(
        ConfigName::TRACE_SAMPLING_RATE, to_string(*conf.trace_sampling_rate),
        ConfigMetadata::Origin::REMOTE_CONFIG);

    TraceSamplerConfig trace_sampler_cfg;
    trace_sampler_cfg.sample_rate = *conf.trace_sampling_rate;
    trace_sampler_cfg.max_per_second = 200;

    auto finalized_trace_sampler_cfg = finalize_config(trace_sampler_cfg);
    if (auto error = finalized_trace_sampler_cfg.if_error()) {
      trace_sampling_metadata.error = *error;
    } else {
      trace_sampler_ =
          std::make_shared<TraceSampler>(*finalized_trace_sampler_cfg, clock_);
    }

    telemetry_conf.emplace_back(std::move(trace_sampling_metadata));
  } else {
    if (!trace_sampler_.is_default()) {
      trace_sampler_.reset();
      telemetry_conf.emplace_back(
          default_config_metadata_[ConfigName::TRACE_SAMPLING_RATE]);
    }
  }

  if (conf.tags) {
    ConfigMetadata tags_metadata(ConfigName::TAGS, join(*conf.tags, ","),
                                 ConfigMetadata::Origin::REMOTE_CONFIG);

    auto parsed_tags = parse_tags(*conf.tags);
    if (auto error = parsed_tags.if_error()) {
      tags_metadata.error = *error;
    } else {
      auto new_span_defaults =
          std::make_shared<SpanDefaults>(*span_defaults_.get());
      new_span_defaults->tags = std::move(*parsed_tags);

      span_defaults_ = new_span_defaults;
    }

    telemetry_conf.emplace_back(std::move(tags_metadata));
  } else {
    if (!span_defaults_.is_default()) {
      span_defaults_.reset();
      telemetry_conf.emplace_back(default_config_metadata_[ConfigName::TAGS]);
    }
  }

  if (conf.report_traces) {
    ConfigMetadata tags_metadata(ConfigName::REPORT_TRACES,
                                 *conf.report_traces ? "true" : "false",
                                 ConfigMetadata::Origin::REMOTE_CONFIG);
    report_traces_ = *conf.report_traces;
  } else {
    if (!report_traces_.is_default()) {
      report_traces_.reset();
      telemetry_conf.emplace_back(
          default_config_metadata_[ConfigName::REPORT_TRACES]);
    }
  }

  return telemetry_conf;
}

std::vector<ConfigMetadata> ConfigManager::reset() {
  std::vector<ConfigMetadata> config_metadata;

  std::lock_guard<std::mutex> lock(mutex_);
  if (!trace_sampler_.is_default()) {
    trace_sampler_.reset();
    config_metadata.emplace_back(
        default_config_metadata_[ConfigName::TRACE_SAMPLING_RATE]);
  }

  if (!span_defaults_.is_default()) {
    span_defaults_.reset();
    config_metadata.emplace_back(default_config_metadata_[ConfigName::TAGS]);
  }

  if (!report_traces_.is_default()) {
    report_traces_.reset();
    config_metadata.emplace_back(
        default_config_metadata_[ConfigName::REPORT_TRACES]);
  }

  return config_metadata;
}

nlohmann::json ConfigManager::config_json() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return nlohmann::json{{"default", to_json(*span_defaults_.get())},
                        {"trace_sampler", trace_sampler_.get()->config_json()},
                        {"report_traces", report_traces_.get()}};
}

}  // namespace tracing
}  // namespace datadog
