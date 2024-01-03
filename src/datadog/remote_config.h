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
  struct State {
    uint64_t targets_version = 1;
    std::string opaque_backend_state;
    Optional<std::string> error_message;
  };

  struct Configuration {
    std::string id;
    std::string hash;
    std::size_t version;
    ConfigUpdate content;
  };

  TracerSignature tracer_signature_;
  ConfigManager& config_manager_;
  std::string client_id_;

  State state_;
  std::unordered_map<std::string, Configuration> applied_config_;

 public:
  RemoteConfigurationManager(const TracerSignature& tracer_signature,
                             ConfigManager& config_manager);

  nlohmann::json make_request_payload();

  void process_response(const nlohmann::json& json);

 private:
  bool is_new_config(StringView config_path, const nlohmann::json& config_meta);

  void apply_config(Configuration config);
  void revert_config(Configuration config);
};

}  // namespace tracing
}  // namespace datadog
