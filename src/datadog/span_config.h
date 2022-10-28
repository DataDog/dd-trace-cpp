#pragma once

// This component provides a `struct`, `SpanConfig`, used to specify span
// properties when a span is created. The following member functions accept a
// `SpanConfig` argument:
//
// - `Tracer::create_span`
// - `Tracer::extract_span`
// - `Span::create_child`
//
// `SpanConfig` contains much of the same information as `SpanDefaults`, but the
// two types have different purposes. `SpanDefaults` are the properties used
// when no corresponding property is specified in a `SpanConfig` argument.
// See `SpanData::apply_config`.

#include <optional>
#include <string>
#include <unordered_map>
#include <variant>

#include "clock.h"

namespace datadog {
namespace tracing {

struct SpanConfig {
  std::optional<std::string> service;
  std::optional<std::string> service_type;
  std::optional<std::string> version;
  std::optional<std::string> environment;
  std::optional<std::string> name;
  std::optional<std::string> resource;
  std::optional<TimePoint> start;
  std::unordered_map<std::string, std::string> tags;
};

}  // namespace tracing
}  // namespace datadog
