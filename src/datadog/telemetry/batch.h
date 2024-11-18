#pragma once

#include <vector>

#include "event_type.h"

namespace datadog {
namespace telemetry {

/*struct AppStarted {*/
/*  std::unordered_map<ConfigName, ConfigMetadata> configuration;*/
/*};*/

class Batch final {
 public:
  std::vector<EventType> events_;
  inline void add_event(EventType et) { events_.emplace_back(et); }
};

}  // namespace telemetry
}  // namespace datadog
