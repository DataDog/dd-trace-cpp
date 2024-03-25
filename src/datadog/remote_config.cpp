#include "remote_config.h"

#include <cassert>
#include <charconv>
#include <exception>
#include <regex>
#include <stdexcept>
#include <string_view>
#include <type_traits>
#include <unordered_set>

#include "base64.h"
#include "config_manager.h"
#include "config_update.h"
#include "json.hpp"
#include "random.h"
#include "string_view.h"
#include "version.h"

using namespace nlohmann::literals;
using namespace std::literals;

namespace datadog::tracing {
namespace {

template <typename V, typename H>
nlohmann::json::array_t subscribed_products_to_json(
    const std::unordered_map<remote_config::Product, V, H>& products) {
  nlohmann::json::array_t res;
  res.reserve(products.size());
  for (auto&& [p, _] : products) {
    res.emplace_back(p.name());
  }
  return res;
}

// Big-endian serialization of the capabilities set
constexpr std::array<uint8_t, sizeof(remote_config::CapabilitiesSet)>
capabilities_byte_array(uint64_t in) {
  std::size_t j = sizeof(in) - 1;
  std::array<uint8_t, sizeof(remote_config::CapabilitiesSet)> res{};
  for (std::size_t i = 0; i < sizeof(in); ++i) {
    res[j--] = in >> (i * 8);
  }

  return res;
}

template <typename K, typename H>
auto subscribed_capabilities(
    const std::unordered_map<K, remote_config::ProductState, H>&
        product_states) {
  remote_config::CapabilitiesSet cap{};
  for (auto&& [_, pstate] : product_states) {
    cap |= pstate.subscribed_capabilities();
  }
  return capabilities_byte_array(cap.value());
}

// Built-in, subscribed-by-default APM_TRACING product listener.
// It builds a ConfigUpdate from the remote config response and calls
// ConfigManager::update with it
class TracingProductListener : public remote_config::ProductListener {
 public:
  TracingProductListener(const TracerSignature& tracer_signature,
                         std::shared_ptr<ConfigManager> config_manager)
      : remote_config::
            ProductListener{remote_config::Product::KnownProducts::APM_TRACING},
        tracer_signature_(tracer_signature),
        config_manager_(std::move(config_manager)) {}

  void on_config_update(const remote_config::ParsedConfigKey& key,
                        const std::string& file_contents,
                        std::vector<ConfigMetadata>& config_update) override {
    const auto config_json = nlohmann::json::parse(file_contents);

    if (!service_env_match(config_json)) {
      return;
    }

    ConfigUpdate const dyn_config =
        parse_dynamic_config(config_json.at("lib_config"));

    std::vector<ConfigMetadata> metadata = config_manager_->update(dyn_config);
    config_update.insert(config_update.end(), metadata.begin(), metadata.end());
    applied_configs_.emplace(key);
  }

  void on_config_remove(const remote_config::ParsedConfigKey& key,
                        std::vector<ConfigMetadata>& config_update) override {
    if (applied_configs_.erase(key) > 0) {
      std::vector<ConfigMetadata> metadata = config_manager_->reset();
      config_update.insert(config_update.end(), metadata.begin(),
                           metadata.end());
    }
  }

  remote_config::CapabilitiesSet capabilities() const override {
    return {
        remote_config::Capability::APM_TRACING_SAMPLE_RATE,
        remote_config::Capability::APM_TRACING_CUSTOM_TAGS,
        remote_config::Capability::APM_TRACING_TRACING_ENABLED,
    };
  }

 private:
  bool service_env_match(const nlohmann::json& config_json) {
    const auto& targeted_service = config_json.find("service_target");
    if (targeted_service == config_json.cend() ||
        !targeted_service->is_object()) {
      return false;
    }
    return targeted_service->at("service").get<std::string_view>() ==
               tracer_signature_.default_service &&
           targeted_service->at("env").get<std::string_view>() ==
               tracer_signature_.default_environment;
  }

  static ConfigUpdate parse_dynamic_config(const nlohmann::json& j) {
    ConfigUpdate config_update;

    if (auto sampling_rate_it = j.find("tracing_sampling_rate");
        sampling_rate_it != j.cend()) {
      config_update.trace_sampling_rate = *sampling_rate_it;
    }

    if (auto tags_it = j.find("tracing_tags"); tags_it != j.cend()) {
      config_update.tags = *tags_it;
    }

    if (auto tracing_enabled_it = j.find("tracing_enabled");
        tracing_enabled_it != j.cend()) {
      if (tracing_enabled_it->is_boolean()) {
        config_update.report_traces = tracing_enabled_it->get<bool>();
      }
    }

    return config_update;
  }

  const TracerSignature& tracer_signature_;  // NOLINT
  std::shared_ptr<ConfigManager> config_manager_;
  std::unordered_set<remote_config::ParsedConfigKey,
                     remote_config::ParsedConfigKey::Hash>
      applied_configs_;
};
}  // namespace

RemoteConfigurationManager::RemoteConfigurationManager(
    const TracerSignature& tracer_signature,
    const std::shared_ptr<ConfigManager>& config_manager,
    std::shared_ptr<Logger> logger)
    : tracer_signature_(tracer_signature),
      config_manager_(config_manager),
      client_id_(uuid()),
      logger_{std::move(logger)} {
  assert(config_manager_);
  auto tracing_listener = std::make_unique<TracingProductListener>(
      tracer_signature, config_manager_);
  add_listener(std::move(tracing_listener));
}

nlohmann::json RemoteConfigurationManager::make_request_payload() {
  // clang-format off
  auto j = nlohmann::json{
    {"client", {
      {"id", client_id_},
      {"products", subscribed_products_to_json(product_states_)},
      {"is_tracer", true},
      {"capabilities", subscribed_capabilities(product_states_)},
      {"client_tracer", {
        {"runtime_id", tracer_signature_.runtime_id.string()},
        {"language", tracer_signature_.library_language},
        {"tracer_version", tracer_signature_.library_version},
        {"service", tracer_signature_.default_service},
        {"env", tracer_signature_.default_environment},
        // missing: tags, extra_services, app_version
      }},
      {"state", {
        {"root_version", 1},
        {"targets_version", next_client_state_.targets_version},
        {"config_states", serialize_config_states()},
        {"has_error", next_client_state_.error.has_value()},
        {"error", next_client_state_.error.value_or("")},
        {"backend_client_state", next_client_state_.backend_client_state},
      }}
    }},
    {"cached_target_files", serialize_cached_target_files()},
  };
  // clang-format on

  return j;
}

nlohmann::json::array_t RemoteConfigurationManager::serialize_config_states()
    const {
  auto&& config_states_nonser = next_client_state_.config_states;

  nlohmann::json::array_t config_states{};
  config_states.reserve(config_states_nonser.size());

  for (const std::shared_ptr<remote_config::ConfigState>& cfg_state :
       config_states_nonser) {
    if (!cfg_state) {
      continue;  // should not happen
    }
    const remote_config::ConfigState& cs = *cfg_state;
    // clang-format off
    config_states.emplace_back(nlohmann::json{
        {"id", cs.id},
        {"version", cs.version},
        {"product", cs.product.name()},
        {"apply_state", static_cast<std::underlying_type_t<decltype(cs.apply_state)>>(cs.apply_state)},
        {"apply_error", cs.apply_error},
    });
    // clang-format on
  }
  return config_states;
}

nlohmann::json RemoteConfigurationManager::serialize_cached_target_files()
    const {
  nlohmann::json::array_t cached_target_files;

  for (auto&& [_, pstate] : product_states_) {
    pstate.for_each_cached_target_file(
        [&cached_target_files](const remote_config::CachedTargetFile& ctf) {
          nlohmann::json::array_t hashes;
          for (auto&& target_file_hash : ctf.hashes) {
            hashes.emplace_back(
                nlohmann::json{{"algorithm", target_file_hash.algorithm},
                               {"hash", target_file_hash.hash}});
          }
          cached_target_files.emplace_back(nlohmann::json{
              {"path", ctf.path},
              {"length", ctf.length},
              {"hashes", std::move(hashes)},
          });
        });
  }

  if (cached_target_files.empty()) {
    // system tests expect (expected?) a null in this case
    // can't use {}-initialization (creates an array)
    return nullptr;
  }

  return cached_target_files;
}

std::vector<ConfigMetadata> RemoteConfigurationManager::process_response(
    nlohmann::json&& json) {
  std::optional<remote_config::RemoteConfigResponse> resp;
  std::vector<std::string> errors;
  std::vector<ConfigMetadata> config_update;

  try {
    auto maybe_resp =
        remote_config::RemoteConfigResponse::from_json(std::move(json));
    if (!maybe_resp) {
      logger_->log_debug(
          "Remote Configuration response is empty (no change)"sv);
      return {};
    }

    logger_->log_debug("Got nonempty Remote Configuration response"sv);
    resp.emplace(std::move(*maybe_resp));
    resp->validate();

    // check if the backend returned configuration for unsubscribed products
    for (const remote_config::ParsedConfigKey& key : resp->client_configs()) {
      if (product_states_.find(key.product()) == product_states_.cend()) {
        throw remote_config::reportable_error(
            "Remote Configuration response contains unknown/unsubscribed "
            "product: " +
            std::string{key.product().name()});
      }
    }

    config_update.reserve(8);

    bool applied_any = false;
    // Apply the configuration is applied product-by-product.
    // ProductState::apply will inspect the returned client_configs and process
    // those that pertain to the product in question.
    for (auto&& [product, pstate] : product_states_) {
      try {
        applied_any = pstate.apply(*resp, config_update) || applied_any;
      } catch (const remote_config::reportable_error& e) {
        logger_->log_error(std::string{"Failed to apply configuration for "} +
                           std::string{product.name()} + ": " + e.what());
        errors.emplace_back(e.what());
      } catch (...) {
        std::terminate();
      }
    }

    if (applied_any) {
      // Now call the "end listeners", which are necessary because
      // AppSec has its configuration split across different products
      for (auto&& listener : config_end_listeners_) {
        try {
          listener();
        } catch (const std::exception& e) {
          logger_->log_error(
              std::string{
                  "Failed to call Remote Configuration end listener: "} +
              e.what());
          errors.emplace_back(e.what());
        }
      }
    }

  } catch (const nlohmann::json::exception& e) {
    std::string error_message = "Ill-formatted Remote Configuration response: ";
    error_message += e.what();
    errors.emplace_back(std::move(error_message));
  } catch (const std::exception& e) {
    errors.emplace_back(e.what());
  }

  if (resp) {
    update_next_state({std::ref(*resp)}, build_error_message(errors));
  } else {
    update_next_state({}, build_error_message(errors));
  }

  return config_update;
}

void RemoteConfigurationManager::add_listener(
    std::unique_ptr<remote_config::ProductListener> listener) {
  auto product = listener->product();
  auto ps_it = product_states_.find(product);
  if (ps_it == product_states_.cend()) {
    ps_it = product_states_.emplace(product, product).first;
  }
  ps_it->second.add_listener(std::move(listener));
}

void RemoteConfigurationManager::add_config_end_listener(
    std::function<void()> listener) {
  config_end_listeners_.emplace_back(std::move(listener));
}

Optional<std::string> RemoteConfigurationManager::build_error_message(
    std::vector<std::string>& errors) {
  if (errors.empty()) {
    return std::nullopt;
  }
  if (errors.size() == 1) {
    return {std::move(errors.front())};
  }

  std::string msg{"Failed to apply configuration due to multiple errors: "};
  for (auto&& e : errors) {
    msg += e;
    msg += "; ";
  }
  return {std::move(msg)};
}

void RemoteConfigurationManager::update_next_state(
    Optional<std::reference_wrapper<const remote_config::RemoteConfigResponse>>
        rcr,
    Optional<std::string> error) {
  uint64_t const new_targets_version =
      rcr ? rcr->get().targets_version() : next_client_state_.targets_version;

  std::vector<std::shared_ptr<remote_config::ConfigState>> config_states;
  for (auto&& [p, pstate] : product_states_) {
    pstate.add_config_states_to(config_states);
  }

  next_client_state_ = ClientState{
      // if there was a global error, we did not apply the configurations fully
      // the system tests expect here the targets version not to be updated
      next_client_state_.root_version,
      error ? next_client_state_.targets_version : new_targets_version,
      config_states,
      std::move(error),
      rcr ? rcr->get().opaque_backend_state()
          : next_client_state_.backend_client_state,
  };
}

namespace remote_config {

namespace {
template <typename SubMatch>
StringView submatch_to_sv(const SubMatch& sub_match) {
  return StringView{&*sub_match.first,
                    static_cast<std::size_t>(sub_match.length())};
}
};  // namespace

void ParsedConfigKey::parse_config_key() {
  std::regex const rgx{"(?:datadog/(\\d+)|employee)/([^/]+)/([^/]+)/([^/]+)"};
  std::smatch smatch;
  if (!std::regex_match(key_, smatch, rgx)) {
    throw reportable_error("Invalid config key: " + key_);
  }

  if (key_[0] == 'd') {
    source_ = "datadog"sv;
    auto [ptr, ec] =
        std::from_chars(&*smatch[1].first, &*smatch[1].second, org_id_);
    if (ec != std::errc{} || ptr != &*smatch[1].second) {
      throw reportable_error("Invalid org_id in config key " + key_ + ": " +
                             std::string{submatch_to_sv(smatch[1])});
    }
  } else {
    source_ = "employee"sv;
    org_id_ = 0;
  }

  StringView const product_sv{submatch_to_sv(smatch[2])};
  product_ = &Product::KnownProducts::for_name(product_sv);

  config_id_ = submatch_to_sv(smatch[3]);
  name_ = submatch_to_sv(smatch[4]);
}

RemoteConfigResponse::RemoteConfigResponse(nlohmann::json full_response,
                                           nlohmann::json targets)
    : json_{std::move(full_response)},
      targets_{std::move(targets)},
      targets_signed_{targets_.at("signed"sv)} {}

std::optional<RemoteConfigResponse> RemoteConfigResponse::from_json(
    nlohmann::json&& json) {
  const auto targets_encoded = json.find("targets"sv);
  if (targets_encoded == json.cend()) {
    // empty response -> no change
    return std::nullopt;
  }

  if (!targets_encoded->is_string()) {
    throw reportable_error(
        "Invalid Remote Configuration response: targets (encoded) is not a "
        "string");
  }

  if (targets_encoded->get<std::string_view>().empty()) {
    // empty response -> no change
    return std::nullopt;
  }

  // if targets is not empty, we need targets.signed
  std::string decoded = base64_decode(targets_encoded->get<std::string_view>());
  if (decoded.empty()) {
    throw reportable_error(
        "Invalid Remote Configuration response: invalid base64 data for "
        "targets");
  }
  auto targets = nlohmann::json::parse(decoded);

  auto t_signed = targets.find("signed"sv);
  if (t_signed == targets.cend()) {
    throw reportable_error(
        "Invalid Remote Configuration response: missing "
        "signed targets with nonempty \"targets\"");
  }

  return RemoteConfigResponse{std::move(json), std::move(targets)};
}

void RemoteConfigResponse::verify_targets_presence() const {
  // files referred to in target_files need to exist in targets.signed.targets
  auto target_files = json_.find("target_files"sv);
  if (target_files == json_.cend()) {
    return;
  }

  if (!target_files->is_array()) {
    throw reportable_error(
        "Invalid Remote Configuration response: target_files is not an array");
  }

  for (auto it = target_files->begin(); it != target_files->end(); ++it) {
    auto path = it->find("path"sv);
    if (path == it->cend() || !path->is_string()) {
      throw reportable_error(
          "Invalid Remote Configuration response: missing "
          "path in element of target_files");
    }

    auto path_sv = path->get<std::string_view>();
    if (!get_target(path_sv)) {
      throw reportable_error(
          "Invalid Remote Configuration response: "
          "target_files[...].path (" +
          std::string{path_sv} +
          ") is a key not present in targets.signed.targets");
    }
  }
}

void RemoteConfigResponse::verify_client_configs() {
  auto&& client_cfgs = json_.find("client_configs"sv);
  if (client_cfgs == json_.end()) {
    return;
  }
  if (!client_cfgs->is_array()) {
    throw reportable_error(
        "Invalid Remote Configuration response: client_configs is no array");
  }
  for (auto&& [_, cc] : client_cfgs->items()) {
    if (!cc.is_string()) {
      throw reportable_error(
          "Invalid Remote Configuration response: client_configs "
          "should be an array of strings");
    }
    client_configs_.emplace_back(cc.get<std::string>());
  }
}

Optional<ConfigTarget> RemoteConfigResponse::get_target(StringView key) const {
  auto&& targets = targets_signed_.at("targets"sv);
  auto&& target = targets.find(key);
  if (target == targets.cend()) {
    return {};
  }
  return ConfigTarget{*target};
}

Optional<std::string> RemoteConfigResponse::get_file_contents(
    const ParsedConfigKey& key) const {
  auto&& target_files = json_.find("target_files"sv);
  if (target_files == json_.cend() || !target_files->is_array()) {
    return {};
  }

  Optional<ConfigTarget> target = get_target(key);
  if (!target) {
    throw std::runtime_error("targets.signed.targets[" +
                             std::string{key.full_key()} +
                             "] was expected to exist at this point");
  }

  for (auto&& file : *target_files) {
    auto&& path = file.at("path"sv).get<std::string>();
    if (path != key.full_key()) {
      continue;
    }

    // TODO check sha256 hash
    auto expected_len = target->length();
    if (expected_len == 0) {
      return {{}};
    }

    const auto raw = file.at("raw"sv).get<std::string_view>();
    auto decoded = base64_decode(raw);
    if (decoded.empty()) {
      throw reportable_error(
          "Invalid Remote Configuration response: target_files[...].raw "
          "is not a valid base64 string");
    }
    if (decoded.length() != expected_len) {
      throw reportable_error(
          "Invalid Remote Configuration response: target_files[...].raw "
          "length (after decoding) does not match the length in "
          "targets.signed.targets. "
          "Expected " +
          std::to_string(expected_len) + ", got " +
          std::to_string(decoded.length()));
    }
    return {std::move(decoded)};
  }

  return {};
}

bool ProductState::apply(const RemoteConfigResponse& response,
                         std::vector<ConfigMetadata>& config_update) {
  const std::vector<std::string> errors;
  std::unordered_set<const ParsedConfigKey*> processed_keys;

  bool changes_detected = false;
  for (auto&& key : response.client_configs()) {
    if (key.product() != product_) {
      continue;
    }

    try {
      const ConfigTarget cfg_target = get_target_or_throw(response, key);
      processed_keys.emplace(&key);

      if (is_target_changed(key, cfg_target)) {
        changes_detected = true;
        const std::string content = get_file_contents_or_throw(response, key);
        call_listeners_apply(response, config_update, key, content);
      }
    } catch (const reportable_error&) {
      throw;
    } catch (const nlohmann::json::exception& e) {
      // if these happen, the response is prob malformed, so it's a global error
      throw reportable_error("JSON error processing key " +
                             std::string{key.full_key()} + ": " + e.what());
    } catch (const std::exception& e) {
      // should not happen; errors are caught in call_listeners_apply
      throw reportable_error("Failed to apply configuration for " +
                             std::string{key.full_key()} + ": " + e.what());
    } catch (...) {
      std::terminate();
    }
  }

  // Process removed configuration
  // per_key_state_ is a map whose keys are the ones we know about
  // processed_keys are the ones we saw in the response
  // So find the ones we know about but didn't see in the response
  for (auto it{per_key_state_.cbegin()}; it != per_key_state_.cend();) {
    bool found{};
    const ParsedConfigKey& key = it->first;
    for (auto&& pk : processed_keys) {
      if (*pk == key) {
        found = true;
        break;
      }
    }
    if (!found) {
      changes_detected = true;
      it = call_listeners_remove(config_update, it);
    } else {
      it++;
    }
  }

  return changes_detected;
}

void ProductState::call_listeners_apply(
    const RemoteConfigResponse& resp,
    std::vector<ConfigMetadata>& config_update, const ParsedConfigKey& key,
    const std::string& file_contents) {
  try {
    for (auto&& l : listeners_) {
      l->on_config_update(key, file_contents, config_update);
    }

    update_config_state(resp, key, {});
  } catch (const reportable_error&) {
    throw;
  } catch (const std::exception& e) {
    update_config_state(resp, key, {{e.what()}});
  } catch (...) {
    std::terminate();
  }
}

ProductState::per_key_state_citerator_t ProductState::call_listeners_remove(
    std::vector<ConfigMetadata>& config_update,
    ProductState::per_key_state_citerator_t it) {
  const ParsedConfigKey& key = it->first;
  try {
    for (auto&& l : listeners_) {
      l->on_config_remove(key, config_update);
    }
  } catch (const reportable_error&) {
    throw;
  } catch (const std::exception& e) {
    // no way to report errors removing a config, as its config_state entry will
    // not be included in the next request. Report it globally
    throw reportable_error("Failed to remove configuration for " +
                           std::string{key.full_key()} + ": " + e.what());
  } catch (...) {
    std::terminate();
  }

  return per_key_state_.erase(it);
}

ConfigTarget ProductState::get_target_or_throw(
    const RemoteConfigResponse& response, const ParsedConfigKey& key) {
  auto&& target = response.get_target(key);
  if (!target) {
    throw reportable_error("Told to apply config for " +
                           std::string{key.full_key()} +
                           ", but no corresponding entry exists in "
                           "targets.targets_signed.targets");
  }
  return *target;
}

std::string ProductState::get_file_contents_or_throw(
    const RemoteConfigResponse& response, const ParsedConfigKey& key) {
  Optional<std::string> maybe_content = response.get_file_contents(key);
  if (!maybe_content) {
    throw reportable_error(
        "Told to apply config for " + std::string{key.full_key()} +
        ", but content not present when it was expected to be (because the new "
        "hash differs from the one last seen, if any)");
  }
  return std::move(*maybe_content);
}

void ProductState::update_config_state(const RemoteConfigResponse& response,
                                       const ParsedConfigKey& key,
                                       Optional<std::string> error) try {
  Optional<ConfigTarget> config_target = response.get_target(key);
  assert(config_target.has_value());

  ConfigState new_config_state{
      std::string{key.config_id()},
      config_target->version(),
      key.product(),
      error ? ConfigState::ApplyState::Error
            : ConfigState::ApplyState::Acknowledged,
      error ? std::move(*error) : std::string{},
  };
  CachedTargetFile new_ctf = config_target->to_cached_target_file(key);

  auto&& state = per_key_state_[key];
  if (!state.config_state) {
    state.config_state =
        std::make_shared<ConfigState>(std::move(new_config_state));
  } else {
    *state.config_state = std::move(new_config_state);
  }
  state.cached_target_file = std::move(new_ctf);
} catch (const std::exception& e) {
  throw reportable_error("Failed to update config state from for " +
                         std::string{key.full_key()} + ": " + e.what());
}

CachedTargetFile ConfigTarget::to_cached_target_file(
    const ParsedConfigKey& key) const {
  auto length = json_.at(std::string_view{"length"}).get<std::uint64_t>();
  std::vector<CachedTargetFile::TargetFileHash> hashes;

  auto&& hashes_json = json_.at(std::string_view{"hashes"});
  if (!hashes_json.is_object()) {
    throw reportable_error(
        "Invalid Remote Configuration response in config_target: "
        "hashes is not an object");
  }
  hashes.reserve(hashes_json.size());
  bool found_sha256 = false;
  for (auto&& [algo, hash] : hashes_json.items()) {
    if (algo == "sha256"sv) {
      found_sha256 = true;
    }
    hashes.emplace_back(
        CachedTargetFile::TargetFileHash{algo, hash.get<std::string>()});
  }
  if (!found_sha256) {
    throw reportable_error(
        "Invalid Remote Configuration response in config_target: "
        "missing sha256 hash for " +
        std::string{key.full_key()});
  }

  return {std::string{key.full_key()}, length, std::move(hashes)};
}
}  // namespace remote_config

}  // namespace datadog::tracing
