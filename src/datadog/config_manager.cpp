#include "config_manager.h"

#include <datadog/telemetry/telemetry.h>

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
using Tags = std::unordered_map<std::string, std::string>;

Expected<Tags> parse_tags_from_sampling_rules(const nlohmann::json& json_tags) {
  assert(json_tags.is_array());

  Tags tags;
  for (const auto& json_tag_entry : json_tags) {
    auto key = json_tag_entry.find("key");
    if (key == json_tag_entry.cend() || key->is_string() == false) {
      std::string err_msg =
          "Failed to parse tags: the required \"key\" field is either missing "
          "or incorrectly formatted. (input: ";
      err_msg += json_tags.dump();
      err_msg += ")";
      return Error{Error::TRACE_SAMPLING_RULES_INVALID_JSON,
                   std::move(err_msg)};
    }

    auto value = json_tag_entry.find("value_glob");
    if (value == json_tag_entry.cend() || value->is_string() == false) {
      std::string err_msg =
          "Failed to parse tags: the required \"value_glob\" field is either "
          "missing or incorrectly formatted. (input: ";
      err_msg += json_tags.dump();
      err_msg += ")";
      return Error{Error::TRACE_SAMPLING_RULES_INVALID_JSON,
                   std::move(err_msg)};
    }

    tags.emplace(*key, *value);
  }

  return tags;
}

Expected<TraceSamplerRule> parse_rule(const nlohmann::json& json_rule) {
  assert(json_rule.is_object());

  auto make_error = [&json_rule](StringView field_name) {
    std::string err_msg = "Failed to parse sampling rule: the required \"";
    append(err_msg, field_name);
    err_msg += "\" field is missing. (input: ";
    err_msg += json_rule.dump();
    err_msg += ")";
    return Error{Error::TRACE_SAMPLING_RULES_INVALID_JSON, std::move(err_msg)};
  };

  const auto make_property_error = [&json_rule](StringView property,
                                                const nlohmann::json& value,
                                                StringView expected_type) {
    std::string message;
    message += "Rule property \"";
    append(message, property);
    message += "\" should have type \"";
    append(message, expected_type);
    message += "\", but has type \"";
    message += value.type_name();
    message += "\": ";
    message += value.dump();
    message += " in rule ";
    message += json_rule.dump();
    return Error{Error::RULE_PROPERTY_WRONG_TYPE, std::move(message)};
  };

  TraceSamplerRule rule;

  // Required: service, resource, sample_rate, provenance.
  if (auto service = json_rule.find("service"); service != json_rule.cend()) {
    if (service->is_string() == false) {
      return make_property_error("service", *service, "string");
    }
    rule.matcher.service = *service;
  } else {
    return make_error("service");
  }

  if (auto resource = json_rule.find("resource");
      resource != json_rule.cend()) {
    if (resource->is_string() == false) {
      return make_property_error("resource", *resource, "string");
    }
    rule.matcher.resource = *resource;
  } else {
    return make_error("resource");
  }

  if (auto sample_rate = json_rule.find("sample_rate");
      sample_rate != json_rule.end()) {
    if (sample_rate->is_number() == false) {
      return make_property_error("sample_rate", *sample_rate, "number");
    }

    auto maybe_rate = Rate::from(*sample_rate);
    if (auto error = maybe_rate.if_error()) {
      return *error;
    }

    rule.rate = *maybe_rate;
  } else {
    return make_error("sample_rate");
  }

  if (auto provenance_it = json_rule.find("provenance");
      provenance_it != json_rule.cend()) {
    if (!provenance_it->is_string()) {
      return make_property_error("provenance", *provenance_it, "string");
    }

    auto provenance = to_lower(provenance_it->get<StringView>());
    if (provenance == "customer") {
      rule.mechanism = SamplingMechanism::REMOTE_RULE;
    } else if (provenance == "dynamic") {
      rule.mechanism = SamplingMechanism::REMOTE_ADAPTIVE_RULE;
    } else {
      std::string err_msg = "Failed to parse sampling rule: unknown \"";
      err_msg += provenance;
      err_msg += "\" value. Expected either \"customer\" or \"dynamic\"";
      return Error{Error::TRACE_SAMPLING_RULES_UNKNOWN_PROPERTY,
                   std::move(err_msg)};
    }
  } else {
    return make_error("provenance");
  }

  // Parse optional fields: name, tags
  if (auto name = json_rule.find("name"); name != json_rule.cend()) {
    if (name->is_string() == false) {
      return make_property_error("name", *name, "string");
    }
    rule.matcher.name = *name;
  }

  if (auto tags = json_rule.find("tags"); tags != json_rule.cend()) {
    if (tags->is_array() == false) {
      return make_property_error("tags", *tags, "array");
    }

    auto maybe_tags = parse_tags_from_sampling_rules(*tags);
    if (auto* error = maybe_tags.if_error()) {
      return *error;
    }

    rule.matcher.tags = std::move(*maybe_tags);
  }

  return rule;
}

Expected<Rules> parse_trace_sampling_rules(const nlohmann::json& json_rules) {
  if (json_rules.is_array() == false) {
    std::string message;
    return Error{Error::TRACE_SAMPLING_RULES_WRONG_TYPE, std::move(message)};
  }

  Rules parsed_rules;
  for (const auto& json_rule : json_rules) {
    auto maybe_rule = parse_rule(json_rule);
    if (auto error = maybe_rule.if_error()) {
      return *error;
    }

    parsed_rules.emplace_back(std::move(*maybe_rule));
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

ConfigManager::ConfigManager(const FinalizedTracerConfig& config)
    : clock_(config.clock),
      default_metadata_(config.metadata),
      trace_sampler_(
          std::make_shared<TraceSampler>(config.trace_sampler, clock_)),
      rules_(config.trace_sampler.rules),
      span_defaults_(std::make_shared<SpanDefaults>(config.defaults)),
      report_traces_(config.report_traces) {}

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

  auto config_update = parse_dynamic_config(config_json.at("lib_config"));

  auto config_metadata = apply_update(config_update);
  telemetry::capture_configuration_change(config_metadata);

  // TODO:
  return nullopt;
}

void ConfigManager::on_revert(const Configuration&) {
  auto config_metadata = apply_update({});
  telemetry::capture_configuration_change(config_metadata);
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

    // Convention: Catch-all rules should ALWAYS be the last in the list.
    // If a catch-all rule already exists, replace it.
    // If NOT, add the new one at the end of the rules list.
    if (rules.empty()) {
      rules.emplace_back(std::move(rule));
    } else {
      if (auto& last_rule = rules.back(); last_rule.matcher == catch_all) {
        last_rule = rule;
      } else {
        rules.emplace_back(std::move(rule));
      }
    }

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
