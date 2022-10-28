#include "collectors.h"

#include <datadog/json.hpp>

#define DEFINE_CONFIG_JSON_METHOD(TYPE)                       \
  void TYPE::config_json(nlohmann::json& destination) const { \
    destination = nlohmann::json::object({{"type", #TYPE}});  \
  }

DEFINE_CONFIG_JSON_METHOD(MockCollector)
DEFINE_CONFIG_JSON_METHOD(MockCollectorWithResponse)
DEFINE_CONFIG_JSON_METHOD(PriorityCountingCollector)
DEFINE_CONFIG_JSON_METHOD(PriorityCountingCollectorWithResponse)
DEFINE_CONFIG_JSON_METHOD(FailureCollector)

#undef DEFINE_CONFIG_JSON_METHOD
