#pragma once

#include <string_view>

namespace datadog {
namespace tracing {

class DictWriter {
 public:
  virtual ~DictWriter() {}

  virtual void set(std::string_view key, std::string_view value) = 0;
};

}  // namespace tracing
}  // namespace datadog
