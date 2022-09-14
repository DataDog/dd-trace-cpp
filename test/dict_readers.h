#pragma once

#include <datadog/dict_reader.h>

#include <string>
#include <unordered_map>

using namespace datadog::tracing;

class MockDictReader : public DictReader {
  const std::unordered_map<std::string, std::string>* map_;

 public:
  explicit MockDictReader(
      const std::unordered_map<std::string, std::string>& map)
      : map_(&map) {}

  std::optional<std::string_view> lookup(std::string_view key) const override {
    auto found = map_->find(std::string(key));
    if (found == map_->end()) {
      return std::nullopt;
    }
    return found->second;
  }

  void visit(
      const std::function<void(std::string_view key, std::string_view value)>&
          visitor) const override {
    for (const auto& [key, value] : *map_) {
      visitor(key, value);
    }
  }
};
