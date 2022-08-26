#pragma once

#include <cstddef>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>

#include "clock.h"
#include "expected.h"

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

  std::optional<std::string_view> environment() const;

  void apply_config(const SpanDefaults& defaults, const SpanConfig& config,
                    Clock clock);
};

Expected<void> msgpack_encode(std::string& destination, const SpanData& span);

}  // namespace tracing
}  // namespace datadog
