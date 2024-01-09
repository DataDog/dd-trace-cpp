#include "datadog_agent_config.h"

#include <algorithm>
#include <chrono>
#include <cstddef>

#include "default_http_client.h"
#include "environment.h"
#include "parse_util.h"
#include "threaded_event_scheduler.h"

namespace datadog {
namespace tracing {

Expected<HTTPClient::URL> DatadogAgentConfig::parse(StringView input) {
  const StringView separator = "://";
  const auto after_scheme = std::search(input.begin(), input.end(),
                                        separator.begin(), separator.end());
  if (after_scheme == input.end()) {
    std::string message;
    message += "Datadog Agent URL is missing the \"://\" separator: \"";
    append(message, input);
    message += '\"';
    return Error{Error::URL_MISSING_SEPARATOR, std::move(message)};
  }

  const StringView scheme = range(input.begin(), after_scheme);
  const StringView supported[] = {"http", "https", "unix", "http+unix",
                                  "https+unix"};
  const auto found =
      std::find(std::begin(supported), std::end(supported), scheme);
  if (found == std::end(supported)) {
    std::string message;
    message += "Unsupported URI scheme \"";
    append(message, scheme);
    message += "\" in Datadog Agent URL \"";
    append(message, input);
    message += "\". The following are supported:";
    for (const auto& supported_scheme : supported) {
      message += ' ';
      append(message, supported_scheme);
    }
    return Error{Error::URL_UNSUPPORTED_SCHEME, std::move(message)};
  }

  const StringView authority_and_path =
      range(after_scheme + separator.size(), input.end());
  // If the scheme is for unix domain sockets, then there's no way to
  // distinguish the path-to-socket from the path-to-resource.  Some
  // implementations require that the forward slashes in the path-to-socket
  // are URL-encoded.  However, URLs that we will be parsing designate the
  // location of the Datadog Agent service, and so do not have a resource
  // location.  Thus, if the scheme is for a unix domain socket, assume that
  // the entire part after the "://" is the path to the socket, and that
  // there is no resource path.
  if (scheme == "unix" || scheme == "http+unix" || scheme == "https+unix") {
    if (authority_and_path.empty() || authority_and_path[0] != '/') {
      std::string message;
      message +=
          "Unix domain socket paths for Datadog Agent must be absolute, i.e. "
          "must begin with a "
          "\"/\". The path \"";
      append(message, authority_and_path);
      message += "\" is not absolute. Error occurred for URL: \"";
      append(message, input);
      message += '\"';
      return Error{Error::URL_UNIX_DOMAIN_SOCKET_PATH_NOT_ABSOLUTE,
                   std::move(message)};
    }
    return HTTPClient::URL{std::string(scheme), std::string(authority_and_path),
                           ""};
  }

  // The scheme is either "http" or "https".  This means that the part after
  // the "://" could be <resource>/<path>, e.g. "localhost:8080/api/v1".
  // Again, though, we're only parsing URLs that designate the location of
  // the Datadog Agent service, and so they will not have a resource
  // location.  Still, let's parse it properly.
  const auto after_authority =
      std::find(authority_and_path.begin(), authority_and_path.end(), '/');
  return HTTPClient::URL{
      std::string(scheme),
      std::string(range(authority_and_path.begin(), after_authority)),
      std::string(range(after_authority, authority_and_path.end()))};
}

Expected<FinalizedDatadogAgentConfig> finalize_config(
    const DatadogAgentConfig& config, const std::shared_ptr<Logger>& logger,
    const Clock& clock) {
  FinalizedDatadogAgentConfig result;

  result.clock = clock;

  if (!config.http_client) {
    result.http_client = default_http_client(logger, clock);
    // `default_http_client` might return a `Curl` instance depending on how
    // this library was built.  If it returns `nullptr`, then there's no
    // built-in default, and so the user must provide a value.
    if (!result.http_client) {
      return Error{Error::DATADOG_AGENT_NULL_HTTP_CLIENT,
                   "DatadogAgent: HTTP client cannot be null."};
    }
  } else {
    result.http_client = config.http_client;
  }

  if (!config.event_scheduler) {
    result.event_scheduler = std::make_shared<ThreadedEventScheduler>();
  } else {
    result.event_scheduler = config.event_scheduler;
  }

  if (config.flush_interval_milliseconds <= 0) {
    return Error{Error::DATADOG_AGENT_INVALID_FLUSH_INTERVAL,
                 "DatadogAgent: Flush interval must be a positive number of "
                 "milliseconds."};
  }
  result.flush_interval =
      std::chrono::milliseconds(config.flush_interval_milliseconds);

  if (config.request_timeout_milliseconds <= 0) {
    return Error{Error::DATADOG_AGENT_INVALID_REQUEST_TIMEOUT,
                 "DatadogAgent: Request timeout must be a positive number of "
                 "milliseconds."};
  }

  if (config.request_timeout_milliseconds <= 0) {
    return Error{Error::DATADOG_AGENT_INVALID_REQUEST_TIMEOUT,
                 "DatadogAgent: Request timeout must be a positive number of "
                 "milliseconds."};
  }

  result.request_timeout =
      std::chrono::milliseconds(config.request_timeout_milliseconds);

  if (config.shutdown_timeout_milliseconds <= 0) {
    return Error{Error::DATADOG_AGENT_INVALID_SHUTDOWN_TIMEOUT,
                 "DatadogAgent: Shutdown timeout must be a positive number of "
                 "milliseconds."};
  }

  result.shutdown_timeout =
      std::chrono::milliseconds(config.shutdown_timeout_milliseconds);

  int rc_poll_interval_seconds =
      config.remote_configuration_poll_interval_seconds;

  if (auto raw_rc_poll_interval_value =
          lookup(environment::DD_REMOTE_CONFIG_POLL_INTERVAL_SECONDS)) {
    auto res = parse_int(*raw_rc_poll_interval_value, 10);
    if (auto error = res.if_error()) {
      return error->with_prefix(
          "DatadogAgent: Remote Configuration poll interval error ");
    }

    rc_poll_interval_seconds = *res;
  }

  if (rc_poll_interval_seconds <= 0) {
    return Error{Error::DATADOG_AGENT_INVALID_REMOTE_CONFIG_POLL_INTERVAL,
                 "DatadogAgent: Remote Configuration poll interval must be a "
                 "positive number of seconds."};
  }

  result.remote_configuration_poll_interval =
      std::chrono::seconds(rc_poll_interval_seconds);

  auto env_host = lookup(environment::DD_AGENT_HOST);
  auto env_port = lookup(environment::DD_TRACE_AGENT_PORT);

  std::string configured_url = config.url;
  if (auto url_env = lookup(environment::DD_TRACE_AGENT_URL)) {
    assign(configured_url, *url_env);
  } else if (env_host || env_port) {
    configured_url.clear();
    configured_url += "http://";
    append(configured_url, env_host.value_or("localhost"));
    configured_url += ':';
    append(configured_url, env_port.value_or("8126"));
  }

  auto url = config.parse(configured_url);
  if (auto* error = url.if_error()) {
    return std::move(*error);
  }
  result.url = *url;

  return result;
}

}  // namespace tracing
}  // namespace datadog
