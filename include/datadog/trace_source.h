#pragma once

#include <datadog/string_view.h>

namespace datadog {
namespace tracing {

/// TBD
enum class Source : char {
  apm = 0x01,
  appsec = 0x02,
  datastream_monitoring = 0x04,
  datajob_monitoring = 0x08,
  database_monitoring = 0x10,
};

/// TBD
bool validate_trace_source(StringView source_str);

/// TBD
inline constexpr StringView to_string(Source source) {
  switch (source) {
    case Source::apm:
      return "01";
    case Source::appsec:
      return "02";
    case Source::database_monitoring:
      return "04";
    case Source::datajob_monitoring:
      return "08";
    case Source::datastream_monitoring:
      return "10";
  }

  return "";
}

}  // namespace tracing
}  // namespace datadog
