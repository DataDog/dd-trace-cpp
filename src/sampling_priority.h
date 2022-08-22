#pragma once

namespace datadog {
namespace tracing {

enum class SamplingPriority {
  // Drop on account of user configuration, such as an explicit sampling
  // rate, sampling rule, or a manual override.
  USER_DROP = -1,
  // Drop on account of a rate conveyed by the Datadog Agent.
  AUTO_DROP = 0,
  // Keep on account of a rate conveyed by the Datadog Agent.
  AUTO_KEEP = 1,
  // Keep on account of user configuration, such as an explicit sampling
  // rate, sampling rule, or a manual override.
  USER_KEEP = 2
};

}  // namespace tracing
}  // namespace datadog
