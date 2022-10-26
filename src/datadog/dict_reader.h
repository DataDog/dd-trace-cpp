#pragma once

// This component provides an interface, `DictReader`, that represents a
// read-only key/value mapping of strings.  It's used when extracting trace
// context from externalized formats: HTTP headers, gRPC metadata, etc.

#include <functional>
#include <optional>
#include <string_view>

namespace datadog {
namespace tracing {

class DictReader {
 public:
  virtual ~DictReader() {}

  // Return the value at the specified `key`, or return `std::nullopt` if there
  // is no value at `key`.
  virtual std::optional<std::string_view> lookup(
      std::string_view key) const = 0;
  
  // Invoke the specified `visitor` once for each key/value pair in this object.
  virtual void visit(
      const std::function<void(std::string_view key, std::string_view value)>&
          visitor) const = 0;
};

}  // namespace tracing
}  // namespace datadog
