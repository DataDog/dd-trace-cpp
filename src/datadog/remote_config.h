#pragma once

// Remote Configuration is a Datadog capability that allows a user to remotely
// configure and change the behaviour of the tracing library.
// The current implementation is restricted to Application Performance
// Monitoring features.
//
// The `RemoteConfigurationManager` class implement the protocol to query,
// process and verify configuration from a remote source. It is also
// responsible for handling configuration updates received from a remote source
// and maintains the state of applied configuration.
// It interacts with the `ConfigManager` to seamlessly apply or revert
// configurations based on responses received from the remote source.

#include <functional>
#include <initializer_list>
#include <memory>
#include <string>
#include <type_traits>

#include "json.hpp"
#include "logger.h"
#include "optional.h"
#include "runtime_id.h"
#include "string_view.h"
#include "trace_sampler_config.h"
#include "tracer_signature.h"

namespace datadog::tracing {

namespace remote_config {
// The ".client.capabilities" field of the remote config request payload
// describes which parts of the library's configuration are supported for remote
// configuration.
//
// It's a bitset, 64 bits wide, where each bit indicates whether the library
// supports a particular feature for remote configuration.
//
// The bitset is encoded in the request as a JSON array of 8 integers, where
// each integer is one byte from the 64 bits. The bytes are in big-endian order
// within the array.
enum class Capability : uint64_t {
  ASM_ACTIVATION = 1 << 1,
  ASM_IP_BLOCKING = 1 << 2,
  ASM_DD_RULES = 1 << 3,
  ASM_EXCLUSIONS = 1 << 4,
  ASM_REQUEST_BLOCKING = 1 << 5,
  ASM_RESPONSE_BLOCKING = 1 << 6,
  ASM_USER_BLOCKING = 1 << 7,
  ASM_CUSTOM_RULES = 1 << 8,
  ASM_CUSTOM_BLOCKING_RESPONSE = 1 << 9,
  ASM_TRUSTED_IPS = 1 << 10,
  ASM_API_SECURITY_SAMPLE_RATE = 1 << 11,
  APM_TRACING_SAMPLE_RATE = 1 << 12,
  APM_TRACING_LOGS_INJECTION = 1 << 13,
  APM_TRACING_HTTP_HEADER_TAGS = 1 << 14,
  APM_TRACING_CUSTOM_TAGS = 1 << 15,
  ASM_PREPROCESSOR_OVERRIDES = 1 << 16,
  ASM_CUSTOM_DATA_SCANNERS = 1 << 17,
  ASM_EXCLUSION_DATA = 1 << 18,
  APM_TRACING_TRACING_ENABLED = 1 << 19,
  APM_TRACING_DATA_STREAMS_ENABLED = 1 << 20,
  ASM_RASP_SQLI = 1 << 21,
  ASM_RASP_LFI = 1 << 22,
  ASM_RASP_SSRF = 1 << 23,
  ASM_RASP_SHI = 1 << 24,
  ASM_RASP_XXE = 1 << 25,
  ASM_RASP_RCE = 1 << 26,
  ASM_RASP_NOSQLI = 1 << 27,
  ASM_RASP_XSS = 1 << 28,
  APM_TRACING_SAMPLE_RULES = 1 << 29,
};

class CapabilitiesSet {
  std::underlying_type_t<Capability> value_{};

 public:
  CapabilitiesSet() = default;
  // NOLINTNEXTLINE
  CapabilitiesSet(Capability c) : value_{static_cast<decltype(value_)>(c)} {}
  CapabilitiesSet(std::initializer_list<Capability> l) {
    for (auto &&c : l) {
      value_ |= static_cast<decltype(value_)>(c);
    }
  }

  CapabilitiesSet &operator|=(CapabilitiesSet other) {
    value_ |= other.value_;
    return *this;
  }

  decltype(value_) value() const { return value_; }
};

class Product {
 public:
  struct KnownProducts;

  StringView name() const { return name_; }
  bool operator==(const Product &p) const { return name_ == p.name_; }
  bool operator!=(const Product &p) const { return !(p == *this); }

  struct Hash {
    std::size_t operator()(const Product &p) const {
#ifdef DD_USE_ABSEIL_FOR_ENVOY
      // 64-bit FNV-1 hash
      auto sv = p.name_;
      static constexpr std::uint64_t offset_basis = 0xcbf29ce484222325;
      static constexpr std::uint64_t prime = 0x100000001b3;

      std::uint64_t hash = offset_basis;
      for (char c : sv) {
        hash ^= static_cast<std::uint8_t>(c);
        hash *= prime;
      }
      return hash;
#else
      return std::hash<std::string_view>()(p.name_);
#endif
    }
  };

 private:
  constexpr explicit Product(StringView name) : name_{name} {}
  StringView name_;
  friend struct KnownProducts;
};

struct Product::KnownProducts {
  static inline constexpr Product AGENT_CONFIG{"AGENT_CONFIG"};
  static inline constexpr Product AGENT_TASK{"AGENT_TASK"};
  static inline constexpr Product APM_TRACING{"APM_TRACING"};
  static inline constexpr Product LIVE_DEBUGGING{"LIVE_DEBUGGING"};
  static inline constexpr Product LIVE_DEBUGGING_SYMBOL_DB{
      "LIVE_DEBUGGING_SYMBOL_DB"};
  static inline constexpr Product ASM{"ASM"};
  static inline constexpr Product ASM_DD{"ASM_DD"};
  static inline constexpr Product ASM_DATA{"ASM_DATA"};
  static inline constexpr Product ASM_FEATURES{"ASM_FEATURES"};
  static inline constexpr Product UNKNOWN{"_UNKNOWN"};
  static inline constexpr auto ALL = {AGENT_CONFIG,
                                      AGENT_TASK,
                                      APM_TRACING,
                                      LIVE_DEBUGGING,
                                      LIVE_DEBUGGING_SYMBOL_DB,
                                      ASM,
                                      ASM_DD,
                                      ASM_DATA,
                                      ASM_FEATURES};

  static const Product &for_name(StringView name) {
    for (auto &&p : KnownProducts::ALL) {
      if (p.name() == name) {
        return p;
      }
    }
    return KnownProducts::UNKNOWN;
  }
};

// A configuration key has the form:
// (datadog/<org_id> | employee)/<PRODUCT>/<config_id>/<name>"
class ParsedConfigKey {
 public:
  explicit ParsedConfigKey(std::string key) : key_{std::move(key)} {
    parse_config_key();
  }
  ParsedConfigKey(const ParsedConfigKey &oth) : ParsedConfigKey(oth.key_) {
    parse_config_key();
  }
  ParsedConfigKey &operator=(const ParsedConfigKey &oth) {
    if (&oth != this) {
      key_ = oth.key_;
      parse_config_key();
    }
    return *this;
  }
  ParsedConfigKey(ParsedConfigKey &&oth) noexcept
      : key_{std::move(oth.key_)},
        source_{oth.source()},
        org_id_{oth.org_id_},
        product_{oth.product_},
        config_id_{oth.config_id_},
        name_{oth.name_} {
    oth.source_ = {};
    oth.org_id_ = 0;
    oth.product_ = &Product::KnownProducts::UNKNOWN;
    oth.config_id_ = {};
    oth.name_ = {};
  }
  ParsedConfigKey &operator=(ParsedConfigKey &&oth) noexcept {
    if (&oth != this) {
      key_ = std::move(oth.key_);
      source_ = oth.source_;
      org_id_ = oth.org_id_;
      product_ = oth.product_;
      config_id_ = oth.config_id_;
      name_ = oth.name_;
      oth.source_ = {};
      oth.org_id_ = 0;
      oth.product_ = &Product::KnownProducts::UNKNOWN;
      oth.config_id_ = {};
      oth.name_ = {};
    }
    return *this;
  }
  ~ParsedConfigKey() = default;

  bool operator==(const ParsedConfigKey &other) const {
    return key_ == other.key_;
  }

  struct Hash {
    std::size_t operator()(const ParsedConfigKey &k) const {
      return std::hash<std::string>()(k.key_);
    }
  };

  // lifetime of return values is that of the data pointer in key_
  StringView full_key() const { return {key_}; }
  StringView source() const { return source_; }
  std::uint64_t org_id() const { return org_id_; }
  Product product() const { return *product_; }
  StringView config_id() const { return config_id_; }
  StringView name() const { return name_; }

 private:
  void parse_config_key();

  std::string key_;
  StringView source_;
  std::uint64_t org_id_{};
  const Product *product_{};
  StringView config_id_;
  StringView name_;
};

// A subset of the information in config_target
struct CachedTargetFile {
  struct TargetFileHash {
    std::string algorithm;
    std::string hash;
  };

  std::string path;
  std::uint64_t length;
  std::vector<TargetFileHash> hashes;

  bool empty() const { return hashes.empty(); }  // NOLINT
  StringView sha256() const {
    auto h = std::find_if(hashes.cbegin(), hashes.cend(), [](const auto &h) {
      return h.algorithm == "sha256";
    });
    if (h == hashes.cend()) {
      return {};
    }
    return {h->hash};
  }
};

struct ConfigState {
  enum class ApplyState {
    Unknown = 0,  // for bc; not to be used
    Unacknowledged =
        1,             // default state. set until the component consuming the
                       // configuration has acknowledged and applied the config
    Acknowledged = 2,  // the configuration has been successfully applied
    Error = 3,         // error applying the configuration
  };

  std::string id;
  std::size_t version;
  Product product;
  ApplyState apply_state{ApplyState::Unacknowledged};
  std::string apply_error;  // not present => empty string
};

class ConfigTarget {
 public:
  explicit ConfigTarget(const nlohmann::json &json) : json_{json} {}

  std::string sha256() const {
    return json_.at("/hashes/sha256"_json_pointer).get<std::string>();
  }
  std::size_t version() const {
    return json_.at("/custom/v"_json_pointer).get<std::size_t>();
  }

  std::size_t length() const {
    return json_.at("/length"_json_pointer).get<std::size_t>();
  }

  CachedTargetFile to_cached_target_file(const ParsedConfigKey &key) const;

 private:
  const nlohmann::json &json_;  // NOLINT
};

class RemoteConfigResponse {
 public:
  static Optional<RemoteConfigResponse> from_json(nlohmann::json &&json);

  void validate() {
    verify_targets_presence();
    verify_client_configs();
  }

  uint64_t targets_version() const {
    return targets_signed_.at(StringView{"version"}).get<uint64_t>();
  }
  std::string opaque_backend_state() const {
    return targets_signed_.at("/custom/opaque_backend_state"_json_pointer)
        .get<std::string>();
  }
  const std::vector<ParsedConfigKey> &client_configs() const {
    return client_configs_;
  }

  Optional<ConfigTarget> get_target(StringView key) const;
  Optional<ConfigTarget> get_target(const ParsedConfigKey &key) const {
    return get_target(key.full_key());
  }

  Optional<std::string> get_file_contents(const ParsedConfigKey &key) const;

 private:
  RemoteConfigResponse(nlohmann::json full_response, nlohmann::json targets);

  void verify_targets_presence() const;
  void verify_client_configs();

  nlohmann::json json_;
  nlohmann::json targets_;                // decoded, parsed
  const nlohmann::json &targets_signed_;  // NOLINT
  std::vector<ParsedConfigKey> client_configs_;
};

class ProductListener {
  Product product_;

 public:
  explicit ProductListener(Product p) : product_{p} {};
  virtual ~ProductListener() = default;
  ProductListener(const ProductListener &) = delete;
  ProductListener &operator=(const ProductListener &) = delete;
  ProductListener(ProductListener &&) = delete;
  ProductListener &operator=(ProductListener &&) = delete;

  virtual void on_config_update(
      const ParsedConfigKey &key, const std::string &content,
      std::vector<ConfigMetadata> &config_updates) = 0;

  virtual void on_config_remove(
      const ParsedConfigKey &key,
      std::vector<ConfigMetadata> &config_updates) = 0;

  [[nodiscard]] virtual CapabilitiesSet capabilities() const = 0;

  Product product() const { return product_; }
};

// errors that should be reported in the ClientState (so not errors applying
// configurations)
class reportable_error : public std::runtime_error {
 public:
  using std::runtime_error::runtime_error;
};

class ProductState {
  struct PerKeyState {
    CachedTargetFile cached_target_file;
    std::shared_ptr<ConfigState> config_state;
  };
  Product product_;
  std::unordered_map<ParsedConfigKey, PerKeyState, ParsedConfigKey::Hash>
      per_key_state_{};
  std::vector<std::unique_ptr<ProductListener>> listeners_;
  std::vector<std::string> errors_;

 public:
  explicit ProductState(Product product) : product_{product} {}
  void add_listener(std::unique_ptr<ProductListener> listener) {
    listeners_.push_back(std::move(listener));
  }

  // throws reportable_error for global errors. All other errors are considered
  // configuration apply errors
  bool apply(const RemoteConfigResponse &response,
             std::vector<ConfigMetadata> &config_update);

  void call_listeners_apply(const RemoteConfigResponse &resp,
                            std::vector<ConfigMetadata> &config_update,
                            const ParsedConfigKey &key,
                            const std::string &file_contents);

  using per_key_state_citerator_t = decltype(per_key_state_)::const_iterator;
  per_key_state_citerator_t call_listeners_remove(
      std::vector<ConfigMetadata> &config_update, per_key_state_citerator_t it);

  void add_config_states_to(
      std::vector<std::shared_ptr<ConfigState>> &config_states) const {
    for (auto &&[key, state] : per_key_state_) {
      config_states.emplace_back(state.config_state);
    }
  };

  CapabilitiesSet subscribed_capabilities() const noexcept {
    CapabilitiesSet caps{};
    for (auto &&listener : listeners_) {
      caps |= listener->capabilities();
    }
    return caps;
  }

  template <typename Func, typename... Args>
  void for_each_cached_target_file(Func &&f, Args &&...args) const {
    for (auto &&[key, state] : per_key_state_) {
      if (state.cached_target_file.empty()) {
        continue;
      }
      std::invoke(std::forward<Func>(f), state.cached_target_file,
                  std::forward<Args>(args)...);
    }
  }

 private:
  static ConfigTarget get_target_or_throw(const RemoteConfigResponse &response,
                                          const ParsedConfigKey &key);

  static std::string get_file_contents_or_throw(
      const RemoteConfigResponse &response, const ParsedConfigKey &key);

  void update_config_state(const RemoteConfigResponse &response,
                           const ParsedConfigKey &key,
                           Optional<std::string> error);

  bool is_target_changed(const ParsedConfigKey &key,
                         const ConfigTarget &new_target) const {
    auto &&st = per_key_state_.find(key);
    if (st == per_key_state_.cend()) {
      return true;  // no previous state
    }
    const CachedTargetFile &ctf = st->second.cached_target_file;
    return ctf.sha256() != new_target.sha256();
  }
};
}  // namespace remote_config

class ConfigManager;

class RemoteConfigurationManager {
  TracerSignature tracer_signature_;
  std::shared_ptr<ConfigManager> config_manager_;
  std::string client_id_;
  std::shared_ptr<Logger> logger_;

  struct ClientState {
    uint64_t root_version = 1UL;
    uint64_t targets_version{};
    std::vector<std::shared_ptr<remote_config::ConfigState>> config_states;
    Optional<std::string> error;  // has_error + error
    std::string backend_client_state;
  };

  ClientState next_client_state_;
  std::unordered_map<remote_config::Product, remote_config::ProductState,
                     remote_config::Product::Hash>
      product_states_;
  std::vector<std::function<void()>> config_end_listeners_;

 public:
  RemoteConfigurationManager(
      const TracerSignature &tracer_signature,
      const std::shared_ptr<ConfigManager> &config_manager,
      std::shared_ptr<Logger> logger);

  void add_listener(std::unique_ptr<remote_config::ProductListener> listener);

  void add_config_end_listener(std::function<void()> listener);

  // Construct a JSON object representing the payload to be sent in a remote
  // configuration request.
  nlohmann::json make_request_payload();

  // Handles the response received from a remote source and udates the internal
  // state accordingly.
  std::vector<ConfigMetadata> process_response(nlohmann::json &&json);

 private:
  static Optional<std::string> build_error_message(
      std::vector<std::string> &errors);

  nlohmann::json::array_t serialize_config_states() const;
  nlohmann::json serialize_cached_target_files() const;

  void update_next_state(
      Optional<
          std::reference_wrapper<const remote_config::RemoteConfigResponse>>
          rcr,
      Optional<std::string> error);
};

}  // namespace datadog::tracing
