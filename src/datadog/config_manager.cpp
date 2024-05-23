#include "config_manager.h"

#include "parse_util.h"
#include "string_util.h"
#include "trace_sampler.h"

namespace datadog {
namespace tracing {
namespace {

using Rules =
    std::unordered_map<SpanMatcher, TraceSamplerRate, SpanMatcher::Hash>;

Expected<Rules> parse_trace_sampling_rules(const nlohmann::json& json_rules) {
  Rules parsed_rules;

  std::string type = json_rules.type_name();
  if (type != "array") {
    std::string message;
    return Error{Error::TRACE_SAMPLING_RULES_WRONG_TYPE, std::move(message)};
  }

  for (const auto& json_rule : json_rules) {
    auto matcher = SpanMatcher::from_json(json_rule);
    if (auto* error = matcher.if_error()) {
      std::string prefix;
      return error->with_prefix(prefix);
    }

    TraceSamplerRate rate;
    if (auto sample_rate = json_rule.find("sample_rate");
        sample_rate != json_rule.end()) {
      type = sample_rate->type_name();
      if (type != "number") {
        std::string message;
        return Error{Error::TRACE_SAMPLING_RULES_SAMPLE_RATE_WRONG_TYPE,
                     std::move(message)};
      }

      auto maybe_rate = Rate::from(*sample_rate);
      if (auto error = maybe_rate.if_error()) {
        return *error;
      }

      rate.value = *maybe_rate;
    }

    if (auto provenance_it = json_rule.find("provenance");
        provenance_it != json_rule.cend()) {
      if (!provenance_it->is_string()) {
        std::string message;
        return Error{Error::TRACE_SAMPLING_RULES_SAMPLE_RATE_WRONG_TYPE,
                     std::move(message)};
      }

      auto provenance = provenance_it->get<std::string_view>();
      if (provenance == "customer") {
        rate.mechanism = SamplingMechanism::REMOTE_RULE;
      } else if (provenance == "dynamic") {
        rate.mechanism = SamplingMechanism::REMOTE_ADAPTIVE_RULE;
      }
    }

    parsed_rules.emplace(std::move(*matcher), std::move(rate));
  }

  return parsed_rules;
}

}  // namespace

ConfigManager::ConfigManager(const FinalizedTracerConfig& config)
    : clock_(config.clock),
      default_metadata_(config.metadata),
      trace_sampler_(
          std::make_shared<TraceSampler>(config.trace_sampler, clock_)),
      rules_(config.trace_sampler.rules),
      span_defaults_(std::make_shared<SpanDefaults>(config.defaults)),
      report_traces_(config.report_traces) {}

std::shared_ptr<TraceSampler> ConfigManager::trace_sampler() {
  std::lock_guard<std::mutex> lock(mutex_);
  return trace_sampler_;
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

  decltype(rules_) rules;

  if (!conf.trace_sampling_rate) {
    auto found = default_metadata_.find(ConfigName::TRACE_SAMPLING_RATE);
    if (found != default_metadata_.cend()) {
      metadata.push_back(found->second);
    }
  } else {
    ConfigMetadata trace_sampling_metadata(
        ConfigName::TRACE_SAMPLING_RATE,
        to_string(*conf.trace_sampling_rate, 1),
        ConfigMetadata::Origin::REMOTE_CONFIG);

    auto rate = Rate::from(*conf.trace_sampling_rate);
    rules[catch_all] = TraceSamplerRate{*rate, SamplingMechanism::RULE};

    metadata.emplace_back(std::move(trace_sampling_metadata));
  }

  if (!conf.trace_sampling_rules) {
    auto found = default_metadata_.find(ConfigName::TRACE_SAMPLING_RULES);
    if (found != default_metadata_.cend()) {
      metadata.emplace_back(found->second);
    }
  } else {
    ConfigMetadata trace_sampling_rules_metadata(
        ConfigName::TRACE_SAMPLING_RULES, conf.trace_sampling_rules->dump(),
        ConfigMetadata::Origin::REMOTE_CONFIG);

    auto maybe_rules = parse_trace_sampling_rules(*conf.trace_sampling_rules);
    if (auto error = maybe_rules.if_error()) {
      trace_sampling_rules_metadata.error = std::move(*error);
    } else {
      rules.merge(*maybe_rules);
    }

    metadata.emplace_back(std::move(trace_sampling_rules_metadata));
  }

  rules.insert(rules_.cbegin(), rules_.cend());
  trace_sampler_->set_rules(rules);

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
  return nlohmann::json{{"defaults", to_json(*span_defaults_.value())},
                        {"trace_sampler", trace_sampler_->config_json()},
                        {"report_traces", report_traces_.value()}};
}

}  // namespace tracing
}  // namespace datadog
