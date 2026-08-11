#pragma once

// Shared helpers for tests that verify the process-level metadata that
// `Tracer` publishes on construction.

#include <filesystem>
#include <map>
#include <sstream>
#include <string>
#include <unordered_map>

#include "../test.h"
#include "platform_util.h"

namespace datadog::test {

// Returns the process tags that `Tracer` is expected to publish: the
// tags from the config + `entrypoint` tags that get added extra.
inline std::map<std::string, std::string> expected_published_process_tags(
    const std::unordered_map<std::string, std::string>& config_process_tags) {
  std::map<std::string, std::string> tags(config_process_tags.begin(),
                                          config_process_tags.end());
  tags.emplace("entrypoint.name", tracing::get_process_name());
  tags.emplace("entrypoint.type", "executable");
  tags.emplace("entrypoint.workdir",
               std::filesystem::current_path().filename().string());
  if (auto path = tracing::get_process_path()) {
    tags.emplace("entrypoint.basedir", path->parent_path().filename().string());
  }
  return tags;
}

// Parses a comma-separated "key:value,key:value" string, as produced by
// `datadog::tracing::join_tags`, back into a map.
inline std::map<std::string, std::string> parse_joined_tags(
    const std::string& joined_tags) {
  std::map<std::string, std::string> out;
  std::istringstream in(joined_tags);
  for (std::string pair; std::getline(in, pair, ',');) {
    const auto colon = pair.find(':');
    REQUIRE(colon != std::string::npos);
    out.emplace(pair.substr(0, colon), pair.substr(colon + 1));
  }
  return out;
}

}  // namespace datadog::test
