#pragma once

#include <cstddef>
#include <optional>
#include <string>
#include <unordered_map>

#include "clock.h"
#include "error.h"

namespace datadog {
namespace tracing {

struct SpanData {
  std::string service;
  std::string name;
  std::string resource;
  std::string type;
  std::uint64_t trace_id = 0;
  std::uint64_t span_id = 0;
  std::uint64_t parent_id = 0;
  TimePoint start;
  Duration duration;
  bool error = false;
  std::unordered_map<std::string, std::string> tags;
  std::unordered_map<std::string, double> numeric_tags;
};

std::optional<Error> msgpack_encode(std::string& destination,
                                    const SpanData& span);

}  // namespace tracing
}  // namespace datadog
