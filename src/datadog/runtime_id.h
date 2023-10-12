#pragma once

// TODO: document

#include <string>

namespace datadog {
namespace tracing {

class RuntimeID {
  std::string uuid_;
  RuntimeID();

 public:
  // Return the canonical textual representation of this ID.
  const std::string& string() const { return uuid_; }

  // Return a pseudo-randomly generated runtime ID. The underlying generator is
  // `random_uint64()` declared in `random.h`.
  static RuntimeID generate();
};

}  // namespace tracing
}  // namespace datadog
