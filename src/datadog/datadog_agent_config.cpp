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

namespace defaults {

constexpr int rc_poll_interval_seconds = 5;
constexpr int flush_interval_milliseconds = 2000;

static const std::string agent_url{"http://localhost:8126"};
constexpr StringView agent_host{"localhost"};
constexpr StringView agent_port{"8126"};

}  // namespace defaults

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

  auto flush_interval_ms = config.flush_interval_milliseconds.value_or(
      defaults::flush_interval_milliseconds);
  if (flush_interval_ms <= 0) {
    return Error{Error::DATADOG_AGENT_INVALID_FLUSH_INTERVAL,
                 "DatadogAgent: Flush interval must be a positive number of "
                 "milliseconds."};
  }

  result.flush_interval = std::chrono::milliseconds(flush_interval_ms);

  int rc_poll_interval_seconds =
      config.remote_configuration_poll_interval_seconds.value_or(
          defaults::rc_poll_interval_seconds);

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

  std::string configured_url = config.url.value_or(defaults::agent_url);
  if (auto url_env = lookup(environment::DD_TRACE_AGENT_URL)) {
    assign(configured_url, *url_env);
  } else if (env_host || env_port) {
    configured_url.clear();
    configured_url += "http://";
    append(configured_url, env_host.value_or(defaults::agent_host));
    configured_url += ':';
    append(configured_url, env_port.value_or(defaults::agent_port));
  }

  auto url = HTTPClient::URL::parse(configured_url);
  if (auto* error = url.if_error()) {
    return std::move(*error);
  }
  result.url = *url;

  return result;
}

}  // namespace tracing
}  // namespace datadog
