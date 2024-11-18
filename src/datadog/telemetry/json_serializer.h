#pragma once

#include "serializer.h"

namespace datadog::telemetry {

class JsonSerializer final : public Serializer<JsonSerializer> {
  std::string buffer_;

 public:
  // TODO(@dmehala): pass-in an allocator
  JsonSerializer() = default;

  void operator()(Batch batch) {
    std::string payload;

    for (const auto& event : batch.events_) {
      switch (event) {
        case EventType::app_started: {
        } break;

        default:
          break;
      }
    }
  }

  const std::string& get_buffer() const { return buffer_; }
};

}  // namespace datadog::telemetry
