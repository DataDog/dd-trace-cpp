#pragma once

// TODO: Document

#include <string>

#include "config_manager.h"
#include "logger.h"
#include "optional.h"
#include "runtime_id.h"
#include "string_view.h"
#include "trace_sampler_config.h"
#include "tracer_signature.h"

namespace datadog {
namespace tracing {

class RemoteConfigurationManager {
  // TODO: document
  struct State {
    uint64_t targets_version = 1;
    std::string opaque_backend_state;
    Optional<std::string> error_message;
  };

  // TODO: document
  struct Configuration {
    std::string id;
    std::string hash;
    std::size_t version;
    ConfigUpdate content;
  };

  TracerSignature tracer_signature_;
  ConfigManager& config_manager_;
  // TODO: document
  std::string client_id_;

  State state_;
  // TODO: document
  std::unordered_map<std::string, Configuration> applied_config_;

 public:
  RemoteConfigurationManager(const TracerSignature& tracer_signature,
                             ConfigManager& config_manager);

  // TODO: document
  nlohmann::json make_request_payload();

  // TODO: document
  void process_response(const nlohmann::json& json);

 private:
  // TODO: document
  bool is_new_config(StringView config_path, const nlohmann::json& config_meta);

  // TODO: document
  void apply_config(Configuration config);
  // TODO: document
  void revert_config(Configuration config);
};

}  // namespace tracing
}  // namespace datadog
