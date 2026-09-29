#pragma once

#include <datadog/expected.h>
#include <datadog/stable_config.h>

#include <string>
#include <vector>

namespace datadog::tracing::stable_config_internal {

struct ProcessInfo {
  std::string language;
  std::vector<std::string> args;
  std::vector<std::string> environment;
};

Expected<StableConfig> load_yaml(StringView local_yaml, StringView fleet_yaml,
                                 const ProcessInfo& process);

}  // namespace datadog::tracing::stable_config_internal
