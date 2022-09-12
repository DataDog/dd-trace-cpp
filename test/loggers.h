#pragma once

#include <datadog/error.h>
#include <datadog/logger.h>

#include <algorithm>
#include <sstream>
#include <string>
#include <variant>
#include <vector>

using namespace datadog::tracing;

struct NullLogger : public Logger {
  void log_error(const LogFunc&) override {}
  void log_startup(const LogFunc&) override {}
  void log_error(const Error&) override {}
  void log_error(std::string_view) override {}
};

struct MockLogger : public Logger {
  struct Entry {
    enum {ERROR, STARTUP} kind;
    std::variant<std::string, Error> payload;
  };

  std::vector<Entry> entries;

  void log_error(const LogFunc& write) override {
      std::ostringstream stream;
      write(stream);
      entries.push_back(Entry{Entry::ERROR, stream.str()});
  }

  void log_startup(const LogFunc& write) override {
      std::ostringstream stream;
      write(stream);
      entries.push_back(Entry{Entry::STARTUP, stream.str()});
  }

  void log_error(const Error& error) override {
      entries.push_back(Entry{Entry::ERROR, error});
  }

  void log_error(std::string_view message) override {
      entries.push_back(Entry{Entry::ERROR, std::string(message)});
  }

  int error_count() const {
    return std::count_if(entries.begin(), entries.end(), [](const Entry& entry) { return entry.kind == Entry::ERROR; });
  }
};
