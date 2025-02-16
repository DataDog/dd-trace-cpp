#pragma once
#include <datadog/telemetry/log.h>

#include <vector>

namespace datadog::telemetry {

template <typename Serializer>
struct Payload {
  void add_logs(const std::vector<LogMessage>& logs) {
    static_cast<Serializer*>(this)->add_logs(logs);
  }
};

}  // namespace datadog::telemetry
