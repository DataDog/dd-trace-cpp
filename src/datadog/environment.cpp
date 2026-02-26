#include <datadog/environment.h>

#include <cstdlib>

#include "json.hpp"
#include "parse_util.h"

namespace datadog {
namespace tracing {
namespace environment {

namespace detail {
Optional<bool> lookup_bool_from_raw(Optional<StringView> value) {
  if (value) {
    return !falsy(*value);
  }
  return nullopt;
}

Expected<Optional<std::uint64_t>> lookup_uint64_from_raw(
    Optional<StringView> value) {
  if (!value) {
    return Optional<std::uint64_t>{};
  }
  auto parsed = parse_uint64(*value, 10);
  if (auto *error = parsed.if_error()) {
    return *error;
  }
  return Optional<std::uint64_t>{*parsed};
}

Expected<Optional<double>> lookup_double_from_raw(Optional<StringView> value) {
  if (!value) {
    return Optional<double>{};
  }
  auto parsed = parse_double(*value);
  if (auto *error = parsed.if_error()) {
    return *error;
  }
  return Optional<double>{*parsed};
}
}  // namespace detail

const VariableSpec &spec(Variable variable) { return variable_specs[variable]; }

StringView name(Variable variable) { return spec(variable).name; }

std::string to_json() {
  auto result = nlohmann::json::object({});

#define ADD_ENV_TO_JSON_IF_SET(DATA, NAME, TYPE, DEFAULT_VALUE)        \
  if (const char *value = std::getenv(VariableTraits<NAME>::name())) { \
    result[VariableTraits<NAME>::name()] = value;                      \
  }

  DD_ENVIRONMENT_VARIABLES(ADD_ENV_TO_JSON_IF_SET, ~)

#undef ADD_ENV_TO_JSON_IF_SET

  return result.dump();
}

}  // namespace environment
}  // namespace tracing
}  // namespace datadog
