#include "span_defaults.h"

namespace datadog {
namespace tracing {

bool operator==(const SpanDefaults& left, const SpanDefaults& right) {
#define EQ(FIELD) left.FIELD == right.FIELD
  return EQ(service) && EQ(service_type) && EQ(environment) && EQ(version) &&
         EQ(name) && EQ(tags);
#undef EQ
}

}  // namespace tracing
}  // namespace datadog
