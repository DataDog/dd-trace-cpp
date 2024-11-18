#pragma once

#include <cassert>
#include <string>

#define DD_QUOTED_IMPL(ARG) #ARG
#define DD_QUOTED(ARG) DD_QUOTED_IMPL(ARG)

namespace datadog::telemetry {

#define DD_TELEMETRY_EVENTS                                                 \
  /*                                                                        \
   * Signal to the backend a new application started.                       \
   * It must contains information the application environment and           \
   * integrations. NOTE: Should only be used once.                          \
   */                                                                       \
  X(app_started)                                                            \
  /*                                                                        \
   * Signal new application dependencies that could not be sent with        \
   * the `app-started` event.                                               \
   * NOTE: call this event only when                                        \
   * `DD_TELEMETRY_DEPENDENCY_COLLECTION_ENABLED` is set.                   \
   */                                                                       \
  X(app_dependencies_loaded)                                                \
  /*                                                                        \
   * Signal newly loaded integrations that could not be sent with           \
   * the `app-started` event. Or integrations, that have already loaded,    \
   * but had their status changed (enabled or disabled).                    \
   */                                                                       \
  X(app_integrations_change)                                                \
  /*                                                                        \
   * Signal the backend that an app is actively running.                    \
   * This event is still required to be sent if another telemetry event has \
   * been sent in the last minute.                                          \
   */                                                                       \
  X(app_heartbeat)                                                          \
  /*                                                                        \
   * Signal the backend that an app is terminating.                         \
   * This event is applicable to environments allowing us to                \
   * intercept                                                              \
   * process signal termination events (SIGINT, SIGQUIT,                    \
   * etc.)                                                                  \
   */                                                                       \
  X(app_closing)

enum class EventType : char {
#define X(NAME) NAME,
  DD_TELEMETRY_EVENTS
#undef X
};

inline std::string_view to_string_view(EventType event) {
#define X(NAME)             \
  case EventType::NAME: {   \
    return DD_QUOTED(NAME); \
  }
  switch (event) { DD_TELEMETRY_EVENTS }
#undef X

  assert(true);
  return "UNKNOWN";
}

inline std::string to_string(EventType event) {
  return std::string(to_string_view(event));
}

}  // namespace datadog::telemetry

#undef DD_QUOTED
#undef DD_QUOTED_IMPL
