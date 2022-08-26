#pragma once

#include <functional>
#include <iosfwd>

namespace datadog {
namespace tracing {

class Logger {
 public:
  using LogFunc = std::function<void(std::ostream&)>;

  virtual void log_error(const LogFunc&) = 0;
  virtual void log_startup(const LogFunc&) = 0;
};

}  // namespace tracing
}  // namespace datadog
