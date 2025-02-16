#pragma once
#include "json.hpp"
#include "serializer.h"

namespace datadog::telemetry {

struct JSONPayload final : public Payload<JSONPayload> {
  nlohmann::json buffer;

  void add_logs(const std::vector<LogMessage>& logs) {
    if (logs.empty()) return;

    auto encoded_logs = nlohmann::json::array();
    for (const auto& log : logs) {
      auto encoded =
          nlohmann::json{{"message", log.message}, {"level", log.level}};
      encoded_logs.emplace_back(std::move(encoded));
    }

    assert(!encoded_logs.empty());

    auto logs_payload = nlohmann::json::object({
        {"request_type", "logs"},
        {"payload",
         nlohmann::json{
             {"logs", encoded_logs},
         }},
    });

    buffer.emplace_back(std::move(logs_payload));
  }
};

}  // namespace datadog::telemetry
