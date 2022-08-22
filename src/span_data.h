#pragma once

#include <cstddef>
#include <memory>
#include <optional>
#include <string>
#include <unordered_map>

#include "clock.h"
#include "error.h"

namespace datadog {
namespace tracing {

struct SpanConfig;
struct SpanDefaults;

struct SpanData {
  std::string service;
  std::string service_type;
  std::string name;
  std::string resource;
  std::uint64_t trace_id = 0;
  std::uint64_t span_id = 0;
  std::uint64_t parent_id = 0;
  TimePoint start;
  Duration duration = Duration::zero();
  bool error = false;
  std::unordered_map<std::string, std::string> tags;
  std::unordered_map<std::string, double> numeric_tags;

  void apply_config(const SpanDefaults& defaults, const SpanConfig& config,
                    Clock clock);
};

std::optional<Error> msgpack_encode(std::string& destination,
                                    const SpanData& span);

}  // namespace tracing
}  // namespace datadog
