#include <datadog/stable_config_loader.h>

#include <cstdint>
#include <memory>
#include <string>

#include "stable_config_ffi.h"

namespace datadog::tracing {
namespace {

dd_trace_stable_config_bytes bytes(StringView value) {
  return {reinterpret_cast<const uint8_t*>(value.data()), value.size()};
}

std::string to_string(dd_trace_stable_config_bytes value) {
  if (value.len == 0) return {};
  return {reinterpret_cast<const char*>(value.data), value.len};
}

}  // namespace

Expected<StableConfig> load_stable_config(StringView language,
                                          StringView local_path,
                                          StringView fleet_path) {
  using Result = std::unique_ptr<dd_trace_stable_config_result,
                                 decltype(&dd_trace_stable_config_drop)>;
  Result result{dd_trace_stable_config_load(bytes(language), bytes(local_path),
                                            bytes(fleet_path)),
                &dd_trace_stable_config_drop};
  if (!result) {
    return Error{Error::OTHER,
                 "Unable to allocate stable configuration result"};
  }
  const auto error = dd_trace_stable_config_error(result.get());
  if (error.data) {
    return Error{Error::OTHER,
                 "Unable to load stable configuration: " + to_string(error)};
  }

  StableConfig config;
  for (std::size_t index = 0;
       index < dd_trace_stable_config_count(result.get()); ++index) {
    dd_trace_stable_config_entry_view entry;
    if (!dd_trace_stable_config_entry(result.get(), index, &entry)) {
      return Error{Error::OTHER, "Unable to read stable configuration entry"};
    }
    config.set(to_string(entry.name),
               {to_string(entry.value),
                entry.source == 1 ? StableConfigSource::FLEET
                                  : StableConfigSource::LOCAL,
                to_string(entry.config_id)});
  }
  return config;
}

Expected<FinalizedTracerConfig> finalize_config_with_stable_config(
    const TracerConfig& config, StringView language, StringView local_path,
    StringView fleet_path) {
  Expected<StableConfig> stable_config =
      load_stable_config(language, local_path, fleet_path);
  if (Error* error = stable_config.if_error()) {
    return *error;
  }
  return finalize_config(config, *stable_config);
}

}  // namespace datadog::tracing
