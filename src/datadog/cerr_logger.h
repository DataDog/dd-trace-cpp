#pragma once

#include <mutex>
#include <sstream>

#include "logger.h"

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
