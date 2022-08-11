#pragma once

#include <string>

namespace datadog {
namespace tracing {

struct Error {
    int code;
    std::string message;
};

}  // namespace tracing
}  // namespace datadog
