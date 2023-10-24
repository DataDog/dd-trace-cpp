#pragma once

#include <string>

#include "config_manager.h"
#include "logger.h"
#include "optional.h"
#include "runtime_id.h"
#include "string_view.h"
#include "trace_sampler_config.h"
#include "tracer_id.h"

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
    RemoteConfigurationUpdate content;
  };

  Logger& logger_;
  TracerId tracer_id_;
  ConfigManager& config_manager_;
  RuntimeID rc_id_;

  State state_;
  std::unordered_map<std::string, Configuration> applied_conf_;

 public:
  RemoteConfigurationManager(Logger& logger, const TracerId& tracer_id,
                             ConfigManager& config_manager);

  nlohmann::json make_request_payload();

  void process_response(const nlohmann::json& json);

  bool is_new_config(StringView config_path, const nlohmann::json& config_meta);

  RemoteConfigurationUpdate parse_config(const nlohmann::json& config);

  // NOTE(@dmehala): will be removed soon.
  void apply_config(Configuration cfg);
  void revert_config(Configuration);
};

}  // namespace tracing
}  // namespace datadog
