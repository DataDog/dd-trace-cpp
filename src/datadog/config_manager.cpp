#include "config_manager.h"

#include "parse_util.h"
#include "string_util.h"
#include "trace_sampler.h"

namespace datadog {
namespace tracing {

ConfigManager::ConfigManager(const FinalizedTracerConfig& config)
    : clock_(config.clock),
      default_metadata_(config.metadata),
      trace_sampler_(
          std::make_shared<TraceSampler>(config.trace_sampler, clock_)),
      span_defaults_(std::make_shared<SpanDefaults>(config.defaults)),
      report_traces_(config.report_traces) {}

std::shared_ptr<TraceSampler> ConfigManager::trace_sampler() {
  std::lock_guard<std::mutex> lock(mutex_);
  return trace_sampler_.value();
}

std::shared_ptr<const SpanDefaults> ConfigManager::span_defaults() {
  std::lock_guard<std::mutex> lock(mutex_);
  return span_defaults_.value();
}

bool ConfigManager::report_traces() {
  std::lock_guard<std::mutex> lock(mutex_);
  return report_traces_.value();
}

std::vector<ConfigMetadata> ConfigManager::update(const ConfigUpdate& conf) {
  std::vector<ConfigMetadata> metadata;

  std::lock_guard<std::mutex> lock(mutex_);

  if (!conf.trace_sampling_rate) {
    reset_config(ConfigName::TRACE_SAMPLING_RATE, trace_sampler_, metadata);
  } else {
    ConfigMetadata trace_sampling_metadata(
        ConfigName::TRACE_SAMPLING_RATE,
        to_string(*conf.trace_sampling_rate, 1),
        ConfigMetadata::Origin::REMOTE_CONFIG);

    TraceSamplerConfig trace_sampler_cfg;
    trace_sampler_cfg.sample_rate = *conf.trace_sampling_rate;

    auto finalized_trace_sampler_cfg = finalize_config(trace_sampler_cfg);
    if (auto error = finalized_trace_sampler_cfg.if_error()) {
      trace_sampling_metadata.error = *error;
    }

    auto trace_sampler =
        std::make_shared<TraceSampler>(*finalized_trace_sampler_cfg, clock_);

    // This reset rate limiting and `TraceSampler` has no `operator==`.
    // TODO: Instead of creating another `TraceSampler`, we should
    // update the default sampling rate.
    trace_sampler_ = std::move(trace_sampler);
    metadata.emplace_back(std::move(trace_sampling_metadata));
  }

  if (!conf.tags) {
    reset_config(ConfigName::TAGS, span_defaults_, metadata);
  } else {
    ConfigMetadata tags_metadata(ConfigName::TAGS, join(*conf.tags, ","),
                                 ConfigMetadata::Origin::REMOTE_CONFIG);

    auto parsed_tags = parse_tags(*conf.tags);
    if (auto error = parsed_tags.if_error()) {
      tags_metadata.error = *error;
    }

    if (*parsed_tags != span_defaults_.value()->tags) {
      auto new_span_defaults =
          std::make_shared<SpanDefaults>(*span_defaults_.value());
      new_span_defaults->tags = std::move(*parsed_tags);

      span_defaults_ = new_span_defaults;
      metadata.emplace_back(std::move(tags_metadata));
    }
  }

  if (!conf.report_traces) {
    reset_config(ConfigName::REPORT_TRACES, report_traces_, metadata);
  } else {
    if (conf.report_traces != report_traces_.value()) {
      report_traces_ = *conf.report_traces;
      metadata.emplace_back(ConfigName::REPORT_TRACES,
                            to_string(*conf.report_traces),
                            ConfigMetadata::Origin::REMOTE_CONFIG);
    }
  }

  return metadata;
}

template <typename T>
void ConfigManager::reset_config(ConfigName name, T& conf,
                                 std::vector<ConfigMetadata>& metadata) {
  if (conf.is_original_value()) return;

  conf.reset();
  metadata.emplace_back(default_metadata_[name]);
}

std::vector<ConfigMetadata> ConfigManager::reset() { return update({}); }

nlohmann::json ConfigManager::config_json() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return nlohmann::json{
      {"defaults", to_json(*span_defaults_.value())},
      {"trace_sampler", trace_sampler_.value()->config_json()},
      {"report_traces", report_traces_.value()}};
}

}  // namespace tracing
}  // namespace datadog
