#pragma once

namespace datadog {
namespace tracing {

enum class SamplingMechanism {
  // There are no sampling rules configured, and the tracer has not yet
  // received any rates from the agent.
  DEFAULT = 0,
  // The sampling decision was due to a sampling rate conveyed by the agent.
  AGENT_RATE = 1,
  // Reserved for future use.
  REMOTE_RATE_AUTO = 2,
  // The sampling decision was due to a matching user-specified sampling rule.
  RULE = 3,
  // The sampling decision was made explicitly by the user, who set a sampling
  // priority.
  MANUAL = 4,
  // Reserved for future use.
  APP_SEC = 5,
  // Reserved for future use.
  REMOTE_RATE_USER_DEFINED = 6,
  // Reserved for future use.
  REMOTE_RATE_EMERGENCY = 7,
  // Individual span kept by a matching span sampling rule when the enclosing
  // trace was dropped.
  SPAN_RULE = 8,
};

}  // namespace tracing
}  // namespace datadog
