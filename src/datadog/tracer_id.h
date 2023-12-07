#pragma once

#include "runtime_id.h"

namespace datadog {
namespace tracing {

// Identify a tracer
struct TracerID {
  RuntimeID runtime_id;
  std::string service;
  std::string environment;
};

}  // namespace tracing
}  // namespace datadog
