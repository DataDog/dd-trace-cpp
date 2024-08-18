#include "config_manager.h"

#include "json_serializer.h"
#include "parse_util.h"
#include "string_util.h"
#include "trace_sampler.h"

namespace datadog {
namespace tracing {
namespace {

nlohmann::json to_json(const SpanDefaults& defaults) {
  auto result = nlohmann::json::object({});
#define TO_JSON(FIELD) \
  if (!defaults.FIELD.empty()) result[#FIELD] = defaults.FIELD
  TO_JSON(service);
  TO_JSON(service_type);
  TO_JSON(environment);
  TO_JSON(version);
  TO_JSON(name);
  TO_JSON(tags);
#undef TO_JSON
  return result;
}

using Rules = std::vector<TraceSamplerRule>;

Expected<Rules> parse_trace_sampling_rules(const nlohmann::json& json_rules) {
  Rules parsed_rules;

  std::string type = json_rules.type_name();
  if (type != "array") {
    std::string message;
    return Error{Error::TRACE_SAMPLING_RULES_WRONG_TYPE, std::move(message)};
  }

  for (const auto& json_rule : json_rules) {
    auto matcher = from_json(json_rule);
    if (auto* error = matcher.if_error()) {
      std::string prefix;
      return error->with_prefix(prefix);
    }

    TraceSamplerRule rule;
    rule.matcher = std::move(*matcher);

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

      rule.rate = *maybe_rate;
    } else {
      return Error{Error::TRACE_SAMPLING_RULES_INVALID_JSON,
                   "Missing \"sample_rate\" field"};
    }

    if (auto provenance_it = json_rule.find("provenance");
        provenance_it != json_rule.cend()) {
      if (!provenance_it->is_string()) {
        std::string message;
        return Error{Error::TRACE_SAMPLING_RULES_SAMPLE_RATE_WRONG_TYPE,
                     std::move(message)};
      }

      auto provenance = to_lower(provenance_it->get<StringView>());
      if (provenance == "customer") {
        rule.mechanism = SamplingMechanism::REMOTE_RULE;
      } else if (provenance == "dynamic") {
        rule.mechanism = SamplingMechanism::REMOTE_ADAPTIVE_RULE;
      } else {
        return Error{Error::TRACE_SAMPLING_RULES_UNKNOWN_PROPERTY,
                     "Unknown \"provenance\" value"};
      }
    } else {
      return Error{Error::TRACE_SAMPLING_RULES_INVALID_JSON,
                   "Missing \"provenance\" field"};
    }

    parsed_rules.emplace_back(std::move(rule));
  }

  return parsed_rules;
}

ConfigManager::Update parse_dynamic_config(const nlohmann::json& j) {
  ConfigManager::Update config_update;

  if (auto sampling_rate_it = j.find("tracing_sampling_rate");
      sampling_rate_it != j.cend() && sampling_rate_it->is_number()) {
    config_update.trace_sampling_rate = sampling_rate_it->get<double>();
  }

  if (auto tags_it = j.find("tracing_tags");
      tags_it != j.cend() && tags_it->is_array()) {
    config_update.tags = tags_it->get<std::vector<StringView>>();
  }

  if (auto tracing_enabled_it = j.find("tracing_enabled");
      tracing_enabled_it != j.cend() && tracing_enabled_it->is_boolean()) {
    config_update.report_traces = tracing_enabled_it->get<bool>();
  }

  if (auto tracing_sampling_rules_it = j.find("tracing_sampling_rules");
      tracing_sampling_rules_it != j.cend() &&
      tracing_sampling_rules_it->is_array()) {
    config_update.trace_sampling_rules = &(*tracing_sampling_rules_it);
  }

  return config_update;
}

}  // namespace

namespace rc = datadog::remote_config;

ConfigManager::ConfigManager(const FinalizedTracerConfig& config,
                             const TracerSignature& tracer_signature,
                             const std::shared_ptr<TracerTelemetry>& telemetry)
    : clock_(config.clock),
      default_metadata_(config.metadata),
      trace_sampler_(
          std::make_shared<TraceSampler>(config.trace_sampler, clock_)),
      rules_(config.trace_sampler.rules),
      span_defaults_(std::make_shared<SpanDefaults>(config.defaults)),
      report_traces_(config.report_traces),
      tracer_signature_(tracer_signature),
      telemetry_(telemetry) {}

rc::Products ConfigManager::get_products() { return rc::product::APM_TRACING; }

rc::Capabilities ConfigManager::get_capabilities() {
  using namespace rc::capability;
  return APM_TRACING_SAMPLE_RATE | APM_TRACING_TAGS | APM_TRACING_ENABLED |
         APM_TRACING_SAMPLE_RULES;
}

Optional<std::string> ConfigManager::on_update(const Configuration& config) {
  if (config.product != rc::product::Flag::APM_TRACING) {
    return nullopt;
  }

  const auto config_json = nlohmann::json::parse(config.content);

  const auto& targeted_service = config_json.at("service_target");
  if (targeted_service.at("service").get<StringView>() !=
          tracer_signature_.default_service ||
      targeted_service.at("env").get<StringView>() !=
          tracer_signature_.default_environment) {
    return "Wrong service targeted";
  }

  auto config_update = parse_dynamic_config(config_json.at("lib_config"));

  auto config_metadata = apply_update(config_update);
  telemetry_->capture_configuration_change(config_metadata);

  // TODO:
  return nullopt;
}

void ConfigManager::on_revert(const Configuration&) {
  auto config_metadata = apply_update({});
  telemetry_->capture_configuration_change(config_metadata);
}

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

std::vector<ConfigMetadata> ConfigManager::apply_update(
    const ConfigManager::Update& conf) {
  std::vector<ConfigMetadata> metadata;

  std::lock_guard<std::mutex> lock(mutex_);

  // NOTE(@dmehala): Sampling rules are generally not well specified.
  //
  // Rules are evaluated in the order they are inserted, which means the most
  // specific matching rule might not be evaluated, even though it should be.
  // For now, we must follow this legacy behavior.
  //
  // Additionally, I exploit this behavior to avoid a merge operation.
  // The resulting array can contain duplicate `SpanMatcher`, but only the first
  // encountered one will be evaluated, acting as an override.
  //
  // Remote Configuration rules will/should always be placed at the begining of
  // the array, ensuring they are evaluated first.
  auto rules = rules_;

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

    TraceSamplerRule rule;
    rule.rate = *rate;
    rule.matcher = catch_all;
    rule.mechanism = SamplingMechanism::RULE;
    rules.emplace(rules.cbegin(), std::move(rule));

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
      rules.insert(rules.cbegin(), maybe_rules->begin(), maybe_rules->end());
    }

    metadata.emplace_back(std::move(trace_sampling_rules_metadata));
  }

  trace_sampler_->set_rules(std::move(rules));

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

nlohmann::json ConfigManager::config_json() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return nlohmann::json{{"defaults", to_json(*span_defaults_.value())},
                        {"trace_sampler", trace_sampler_->config_json()},
                        {"report_traces", report_traces_.value()}};
}

}  // namespace tracing
}  // namespace datadog
