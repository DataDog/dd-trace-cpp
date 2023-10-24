#pragma once

#include "runtime_id.h"

namespace datadog {
namespace tracing {
// To identify a tracer
struct TracerId {
  RuntimeID runtime_id;
  std::string service;
  std::string environment;
};

}  // namespace tracing
}  // namespace datadog
