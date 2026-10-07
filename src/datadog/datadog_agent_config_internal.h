#pragma once

#include <datadog/datadog_agent_config.h>

#include <filesystem>
#include <utility>

namespace datadog::tracing {

// The well-known path where the Datadog Agent listens on a Unix domain socket.
// Single-step instrumentation mounts the socket here.
inline constexpr char default_agent_socket_path[] =
    "/var/run/datadog/apm.socket";

// Return the Agent URL configured by the environment, or null if the
// environment does not configure one. `DD_TRACE_AGENT_URL` wins over
// `DD_AGENT_HOST` and `DD_TRACE_AGENT_PORT`. Empty values count as unset.
Optional<std::string> build_agent_url_from_environment_variables();

// Return the origin and value of the Agent URL, given the URL from the
// environment, the URL from programmatic configuration, and the path where the
// Agent might listen on a Unix domain socket.
std::pair<ConfigMetadata::Origin, std::string> select_agent_url(
    const Optional<std::string>& environment_url,
    const Optional<std::string>& programmatic_url,
    const std::filesystem::path& default_socket_path);

}  // namespace datadog::tracing
