#include "datadog_agent_config.h"

#include <algorithm>
#include <cstddef>

#include "default_http_client.h"
#include "threaded_event_scheduler.h"

namespace datadog {
namespace tracing {
namespace {

template <typename BeginIterator, typename EndIterator>
std::string_view range(BeginIterator begin, EndIterator end) {
  return std::string_view{begin, std::size_t(end - begin)};
}

}  // namespace

Expected<HTTPClient::URL> DatadogAgentConfig::parse(std::string_view input) {
  const std::string_view separator = "://";
  const auto after_scheme = std::search(input.begin(), input.end(),
                                        separator.begin(), separator.end());
  if (after_scheme == input.end()) {
    std::string message;
    message += "Datadog Agent URL is missing the \"://\" separator: \"";
    message += input;
    message += '\"';
    return Error{Error::URL_MISSING_SEPARATOR, std::move(message)};
  }

  const std::string_view scheme = range(input.begin(), after_scheme);
  const std::string_view supported[] = {"http", "https", "unix", "http+unix",
                                        "https+unix"};
  const auto found =
      std::find(std::begin(supported), std::end(supported), scheme);
  if (found == std::end(supported)) {
    std::string message;
    message += "Unsupported URI scheme \"";
    message += scheme;
    message += "\" in Datadog Agent URL \"";
    message += input;
    message += "\". The following are supported:";
    for (const auto& supported_scheme : supported) {
      message += ' ';
      message += supported_scheme;
    }
    return Error{Error::URL_UNSUPPORTED_SCHEME, std::move(message)};
  }

  const std::string_view authority_and_path =
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
      message += authority_and_path;
      message += "\" is not absolute. Error occurred for URL: \"";
      message += input;
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
    const DatadogAgentConfig& config) {
  FinalizedDatadogAgentConfig result;

  if (!config.http_client) {
    result.http_client = default_http_client();
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

  auto url = config.parse(config.url);
  if (auto* error = url.if_error()) {
    return std::move(*error);
  }
  result.url = *url;

  return result;
}

}  // namespace tracing
}  // namespace datadog
