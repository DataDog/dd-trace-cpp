#pragma once

#include <datadog/dict_writer.h>

#include <string>
#include <string_view>
#include <unordered_map>

using namespace datadog::tracing;

struct MockDictWriter : public DictWriter {
  std::unordered_map<std::string, std::string> items;

  void set(std::string_view key, std::string_view value) override {
    items.insert_or_assign(std::string(key), std::string(value));
  }
};
