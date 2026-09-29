#pragma once

#include <datadog/expected.h>
#include <datadog/stable_config.h>
#include <datadog/tracer_config.h>

namespace datadog::tracing {

// Load local and fleet stable configuration through libdatadog.
// Empty paths select libdatadog's platform defaults.
Expected<StableConfig> load_stable_config(StringView language,
                                          StringView local_path = {},
                                          StringView fleet_path = {});

// Load stable config and use it to finalize the tracer configuration.
Expected<FinalizedTracerConfig> finalize_config_with_stable_config(
    const TracerConfig& config, StringView language, StringView local_path = {},
    StringView fleet_path = {});

}  // namespace datadog::tracing
