#pragma once

#include <functional>
#include <optional>
#include <string_view>

namespace datadog {
namespace tracing {

class DictReader {
 public:
  virtual ~DictReader() {}

  virtual std::optional<std::string_view> lookup(
      std::string_view key) const = 0;
  virtual void visit(
      const std::function<void(std::string_view key, std::string_view value)>&
          visitor) const = 0;
};

}  // namespace tracing
}  // namespace datadog
