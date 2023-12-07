#include "remote_config.h"

#include <stdint.h>

#include <type_traits>
#include <unordered_set>

#include "base64.h"
#include "json.hpp"
#include "random.h"
#include "version.h"

using namespace nlohmann::literals;

namespace datadog {
namespace tracing {
namespace {

enum CapabilitiesFlag : uint64_t {
  APM_TRACING_SAMPLE_RATE = 1 << 12,
};

constexpr std::array<uint8_t, sizeof(uint64_t)> capabilities_byte_array(
    uint64_t in) {
  std::size_t j = sizeof(in) - 1;
  std::array<uint8_t, sizeof(uint64_t)> res{};
  for (std::size_t i = 0; i < sizeof(in); ++i) {
    res[j--] = in >> (i * 8);
  }

  return res;
}

constexpr StringView k_apm_product = "APM_TRACING";

constexpr std::array<uint8_t, sizeof(uint64_t)> k_apm_capabilities =
    capabilities_byte_array((uint64_t)0 | APM_TRACING_SAMPLE_RATE);

}  // namespace

void from_json(const nlohmann::json& j, ConfigManager::Update& out) {
  if (auto sampling_rate_it = j.find("tracing_sampling_rate");
      sampling_rate_it != j.cend()) {
    TraceSamplerConfig trace_sampler_cfg;
    trace_sampler_cfg.sample_rate = *sampling_rate_it;
    out.trace_sampler = trace_sampler_cfg;
  }
}

RemoteConfigurationManager::RemoteConfigurationManager(
    const TracerId& tracer_id, ConfigManager& config_manager)
    : tracer_id_(tracer_id),
      config_manager_(config_manager),
      client_id_(uuid()) {}

bool RemoteConfigurationManager::is_new_config(
    StringView config_path, const nlohmann::json& config_meta) {
  auto it = applied_config_.find(std::string{config_path});
  if (it == applied_config_.cend()) return true;

  return it->second.hash !=
         config_meta.at("/hashes/sha256"_json_pointer).get<StringView>();
}

nlohmann::json RemoteConfigurationManager::make_request_payload() {
  // clang-format off
  auto j = nlohmann::json{
    {"client", {
      {"id", client_id_},
      {"products", nlohmann::json::array({k_apm_product})},
      {"is_tracer", true},
      {"capabilities", k_apm_capabilities},
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

  if (!applied_config_.empty()) {
    auto config_states = nlohmann::json::array();
    for (const auto& [_, config] : applied_config_) {
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
        base64_decode(json.at("targets").get<StringView>()));

    state_.targets_version = targets.at("/signed/version"_json_pointer);
    state_.opaque_backend_state =
        targets.at("/signed/custom/opaque_backend_state"_json_pointer);

    const auto client_configs_it = json.find("client_configs");

    // `client_configs` is absent => remove previously applied configuration if
    // any applied.
    if (client_configs_it == json.cend()) {
      if (!applied_config_.empty()) {
        std::for_each(applied_config_.cbegin(), applied_config_.cend(),
                      [this](const auto it) { revert_config(it.second); });
        applied_config_.clear();
      }
      return;
    }

    // Keep track of config path received to know which ones to revert.
    std::unordered_set<std::string> visited_config;
    visited_config.reserve(client_configs_it->size());

    for (const auto& client_config : *client_configs_it) {
      auto config_path = client_config.get<StringView>();
      visited_config.emplace(config_path);

      const auto& config_metadata =
          targets.at("/signed/targets"_json_pointer).at(config_path);
      if (!contains(config_path, k_apm_product) ||
          !is_new_config(config_path, config_metadata)) {
        continue;
      }

      // NOTE(@dmehala): is it worth indexing first?
      const auto& target_files = json.at("/target_files"_json_pointer);
      auto target_it = std::find_if(
          target_files.cbegin(), target_files.cend(),
          [&config_path](const auto& j) {
            return j.at("/path"_json_pointer).template get<StringView>() ==
                   config_path;
          });

      if (target_it == target_files.cend()) {
        state_.error_message =
            "Missing configuration from Remote Configuration response";
        return;
      }

      const auto config_json = nlohmann::json::parse(
          base64_decode(target_it.value().at("raw").get<StringView>()));

      const auto& targeted_service = config_json.at("service_target");
      if (targeted_service.at("service").get<StringView>() !=
              tracer_id_.service ||
          targeted_service.at("env").get<StringView>() !=
              tracer_id_.environment) {
        continue;
      }

      Configuration new_config;
      new_config.hash = config_metadata.at("/hashes/sha256"_json_pointer);
      new_config.id = config_json.at("id");
      new_config.version = config_json.at("revision");
      new_config.content = ConfigManager::Update(config_json.at("lib_config"));

      apply_config(new_config);
      applied_config_[std::string{config_path}] = new_config;
    }

    // Applied configuration not present must be reverted.
    for (auto it = applied_config_.cbegin(); it != applied_config_.cend();) {
      if (!visited_config.count(it->first)) {
        revert_config(it->second);
        it = applied_config_.erase(it);
      } else {
        it++;
      }
    }
  } catch (const nlohmann::json::exception& e) {
    std::string error_message = "Ill-formatted Remote Configuration response: ";
    error_message += e.what();

    state_.error_message = std::move(error_message);
  }
}

void RemoteConfigurationManager::apply_config(Configuration config) {
  config_manager_.update(config.content);
}

void RemoteConfigurationManager::revert_config(Configuration) {
  config_manager_.reset();
}

}  // namespace tracing
}  // namespace datadog
