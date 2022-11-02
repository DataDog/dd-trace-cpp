#include "propagation_styles.h"

#include <string>
#include <vector>

#include "json.hpp"

namespace datadog {
namespace tracing {

nlohmann::json to_json(const PropagationStyles& styles) {
  std::vector<std::string> selected_names;
  if (styles.datadog) {
    selected_names.emplace_back("datadog");
  }
  if (styles.b3) {
    selected_names.emplace_back("B3");
  }
  return selected_names;
}

}  // namespace tracing
}  // namespace datadog
