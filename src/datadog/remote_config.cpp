#include "remote_config.h"

#include <unordered_set>

#include "base64.h"
#include "json.hpp"
#include "version.h"

namespace datadog {
namespace tracing {
namespace {

inline constexpr StringView k_apm_product = "APM_TRACING";

inline constexpr std::array<StringView, 2> k_unsupported_cfg{
    "tracing_header_tags", "log_injection_enabled"};

}  // namespace

void from_json(const nlohmann::json& j, RemoteConfigurationUpdate& out) {
  if (auto sampling_rate_it = j.find("tracing_sampling_rate");
      sampling_rate_it != j.cend()) {
    TraceSamplerConfig trace_sampler_cfg;
    trace_sampler_cfg.sample_rate = *sampling_rate_it;
    out.trace_sampler = trace_sampler_cfg;
  }
}

RemoteConfigurationManager::RemoteConfigurationManager(
    Logger& logger, const TracerId& tracer_id, ConfigManager& config_manager)
    : logger_(logger),
      tracer_id_(tracer_id),
      config_manager_(config_manager),
      rc_id_(RuntimeID::generate()) {}

bool RemoteConfigurationManager::is_new_config(
    StringView config_path, const nlohmann::json& config_meta) {
  auto it = applied_conf_.find(std::string{config_path});
  if (it == applied_conf_.cend()) return true;

  return it->second.hash != config_meta["hashes"]["sha256"].get<StringView>();
}

nlohmann::json RemoteConfigurationManager::make_request_payload() {
  // clang-format off
    auto j = nlohmann::json{
      {"client", {
        {"id", rc_id_.string()},
        {"products", nlohmann::json::array({k_apm_product})},
        {"is_tracer", true},
        {"client_tracer", {
          {"runtime_id", tracer_id_.runtime_id.string()},
          {"language", "cpp"},
          {"tracer_version", tracer_version},
          {"service", tracer_id_.service},
          {"env", tracer_id_.environment}
        }},
        {"state", {
          {"root_version", 1},
          {"targets_version", state_.targets_version},
          {"backend_client_state", state_.opaque_backend_state}
        }}
      }}
    };
  // clang-format on

  if (!applied_conf_.empty()) {
    auto config_states = nlohmann::json::array();
    for (const auto& [_, config] : applied_conf_) {
      config_states.emplace_back(nlohmann::json{{"id", config.id},
                                                {"version", config.version},
                                                {"product", k_apm_product}});
    }

    j["config_states"] = config_states;
  }

  if (state_.error_message) {
    j["has_error"] = true;
    j["error"] = *state_.error_message;
  }

  return j;
}

void RemoteConfigurationManager::process_response(const nlohmann::json& json) {
  state_.error_message = nullopt;

  try {
    const auto targets = nlohmann::json::parse(
        base64::decode(json["targets"].get<StringView>()));

    state_.targets_version = targets["signed"]["version"];
    state_.opaque_backend_state =
        targets["signed"]["custom"]["opaque_backend_state"];

    const auto client_configs_it = json.find("client_configs");

    // `client_configs` is absent => remove previously applied configuration if
    // any applied.
    if (client_configs_it == json.cend()) {
      if (!applied_conf_.empty()) {
        std::for_each(applied_conf_.cbegin(), applied_conf_.cend(),
                      [this](const auto it) { revert_config(it.second); });
        applied_conf_.clear();
      }
      return;
    }

    // Keep track of config path received to know which ones to revert.
    std::unordered_set<std::string> visited_conf;
    visited_conf.reserve(client_configs_it->size());

    for (const auto& client_config : *client_configs_it) {
      auto config_path = client_config.get<StringView>();
      visited_conf.emplace(config_path);

      const auto& config_metadata = targets["signed"]["targets"][config_path];
      if (!contains(config_path, k_apm_product) ||
          !is_new_config(config_path, config_metadata)) {
        continue;
      }

      // NOTE(@dmehala): is it worth indexing first?
      const auto& target_files = json["target_files"];
      auto target_it = std::find_if(
          target_files.cbegin(), target_files.cend(),
          [&config_path](const auto& j) {
            return j["path"].template get<StringView>() == config_path;
          });

      if (target_it == target_files.cend()) {
        state_.error_message =
            "Missing configuration from Remote Configuration response";
        return;
      }

      const auto config_json = nlohmann::json::parse(
          base64::decode(target_it.value()["raw"].get<StringView>()));

      const auto& targeted_service = config_json["service_target"];
      if (targeted_service["service"].get<StringView>() != tracer_id_.service ||
          targeted_service["env"].get<StringView>() != tracer_id_.environment) {
        continue;
      }

      Configuration new_conf;
      new_conf.hash = config_metadata["hashes"]["sha256"];
      new_conf.id = config_json["id"];
      new_conf.version = config_json["revision"];
      new_conf.content = parse_config(config_json);

      apply_config(new_conf);
      applied_conf_[std::string{config_path}] = new_conf;
    }

    // Applied configuration not present must be reverted.
    for (auto it = applied_conf_.cbegin(); it != applied_conf_.cend();) {
      if (!visited_conf.count(it->first)) {
        revert_config(it->second);
        it = applied_conf_.erase(it);
      } else {
        it++;
      }
    }
  } catch (const nlohmann::json::exception& e) {
    state_.error_message = "Ill-formatted Remote Configuration response";
  }
}

RemoteConfigurationUpdate RemoteConfigurationManager::parse_config(
    const nlohmann::json& config) {
  const auto& lib_config = config["lib_config"];

  // NOTE(@dmehala): I thought it would be nice to warn on the usage of
  // unsupported config. For the team: what's your thought?
  for (auto config_name : k_unsupported_cfg) {
    if (lib_config.count(config_name)) {
      logger_.log_error([&config_name](auto& stream) {
        stream << "Remote Configuration field \"" << config_name
               << "\" not supported";
      });
    }
  }

  RemoteConfigurationUpdate res = lib_config;
  return res;
}

void RemoteConfigurationManager::apply_config(Configuration cfg) {
  config_manager_.update(cfg.content);
}

void RemoteConfigurationManager::revert_config(Configuration) {
  config_manager_.reset();
}

}  // namespace tracing
}  // namespace datadog
