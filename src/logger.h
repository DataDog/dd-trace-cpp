#pragma once

#include <functional>
#include <memory>
#include <string_view>

namespace datadog {
namespace tracing {

class Logger {
    virtual void print(std::string_view message) = 0;
};

using LogHandler = std::function<void(std::string_view message)>;

class DefaultLogger : public Logger {
    LogHandler handler_;

  public:
    explicit DefaultLogger(const LogHandler& handler)
    : handler_(handler) {
    }

    virtual void print(std::string_view message) override {
        handler_(message);
    }
};

inline std::shared_ptr<Logger> make_logger(const LogHandler& handler) {
    return std::make_shared<DefaultLogger>(handler);
}

}  // namespace tracing
}  // namespace datadog
