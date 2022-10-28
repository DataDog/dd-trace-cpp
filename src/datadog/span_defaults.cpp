#include "span_defaults.h"

#include "json.hpp"

namespace datadog {
namespace tracing {

bool operator==(const SpanDefaults& left, const SpanDefaults& right) {
#define EQ(FIELD) left.FIELD == right.FIELD
  return EQ(service) && EQ(service_type) && EQ(environment) && EQ(version) &&
         EQ(name) && EQ(tags);
#undef EQ
}

void to_json(nlohmann::json& destination, const SpanDefaults& defaults) {
  destination = nlohmann::json::object({});
#define TO_JSON(FIELD) \
  if (!defaults.FIELD.empty()) destination[#FIELD] = defaults.FIELD
  TO_JSON(service);
  TO_JSON(service_type);
  TO_JSON(environment);
  TO_JSON(version);
  TO_JSON(name);
  TO_JSON(tags);
#undef TO_JSON
}

}  // namespace tracing
}  // namespace datadog
