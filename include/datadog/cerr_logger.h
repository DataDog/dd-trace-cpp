#pragma once

// This component provides a class, `CerrLogger`, that implements the `Logger`
// interface from `logger.h`.  `CerrLogger` prints to `std::cerr`, which is
// typically an unbuffered stream to the standard error file.

#include <datadog/logger.h>

#include <mutex>
#include <sstream>

namespace datadog {
namespace tracing {

class CerrLogger : public Logger {
  std::mutex mutex_;
  std::ostringstream stream_;

 public:
  void log_error(const LogFunc&) override;
  void log_startup(const LogFunc&) override;
  using Logger::log_error;  // expose the non-virtual overloads

 private:
  void log(const LogFunc&);
};

}  // namespace tracing
}  // namespace datadog
