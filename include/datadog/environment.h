#pragma once

// This component provides a registry of all environment variables that can be
// used to configure this library.
//
// Each `enum Variable` denotes an environment variable.  The enum value names
// are the same as the names of the environment variables.
//
// `name` returns the name of a specified `Variable`.
//
// `lookup` retrieves the value of `Variable` in the environment.

#include <datadog/environment_registry.h>
#include <datadog/expected.h>
#include <datadog/optional.h>
#include <datadog/string_view.h>

#include <cstdint>
#include <cstdlib>

namespace datadog {
namespace tracing {
namespace environment {

enum class VariableType {
  STRING,
  BOOLEAN,
  INT,
  DECIMAL,
  ARRAY,
  MAP,
};

struct VariableSpec {
  StringView name;
  VariableType type;
};

#define VARIABLE_ENUM_VALUE(DATA, NAME, TYPE, DEFAULT_VALUE) NAME,

enum Variable { DD_ENVIRONMENT_VARIABLES(VARIABLE_ENUM_VALUE, ~) };

// Quoting a macro argument requires this two-step.
#define QUOTED_IMPL(ARG) #ARG
#define QUOTED(ARG) QUOTED_IMPL(ARG)

#define VARIABLE_SPEC_WITH_COMMA(DATA, NAME, TYPE, DEFAULT_VALUE) \
  VariableSpec{StringView{QUOTED(NAME)}, VariableType::TYPE},

inline const VariableSpec variable_specs[] = {
    DD_ENVIRONMENT_VARIABLES(VARIABLE_SPEC_WITH_COMMA, ~)};

template <VariableType type>
struct LookupResultByType;

template <>
struct LookupResultByType<VariableType::STRING> {
  using type = Optional<StringView>;
};

template <>
struct LookupResultByType<VariableType::BOOLEAN> {
  using type = Optional<bool>;
};

template <>
struct LookupResultByType<VariableType::INT> {
  using type = Expected<Optional<std::uint64_t>>;
};

template <>
struct LookupResultByType<VariableType::DECIMAL> {
  using type = Expected<Optional<double>>;
};

template <>
struct LookupResultByType<VariableType::ARRAY> {
  using type = Optional<StringView>;
};

template <>
struct LookupResultByType<VariableType::MAP> {
  using type = Optional<StringView>;
};

template <Variable variable>
struct VariableTraits;

#define VARIABLE_TRAITS_VALUE(DATA, NAME, TYPE, DEFAULT_VALUE)              \
  template <>                                                               \
  struct VariableTraits<NAME> {                                             \
    static constexpr VariableType variable_type = VariableType::TYPE;       \
    static constexpr const char *name() { return QUOTED(NAME); }            \
    using lookup_result = typename LookupResultByType<variable_type>::type; \
  };

DD_ENVIRONMENT_VARIABLES(VARIABLE_TRAITS_VALUE, ~)

template <Variable variable>
using LookupResult = typename VariableTraits<variable>::lookup_result;

namespace detail {
template <VariableType>
inline constexpr bool unsupported_variable_type_v = false;

template <Variable variable>
Optional<StringView> lookup_raw() {
  const char *value = std::getenv(VariableTraits<variable>::name());
  if (!value) {
    return nullopt;
  }
  return StringView{value};
}

Optional<bool> lookup_bool_from_raw(Optional<StringView> value);
Expected<Optional<std::uint64_t>> lookup_uint64_from_raw(
    Optional<StringView> value);
Expected<Optional<double>> lookup_double_from_raw(Optional<StringView> value);
}  // namespace detail

template <Variable variable>
LookupResult<variable> lookup() {
  constexpr VariableType type = VariableTraits<variable>::variable_type;
  const auto raw = detail::lookup_raw<variable>();
  if constexpr (type == VariableType::STRING || type == VariableType::ARRAY ||
                type == VariableType::MAP) {
    return raw;
  } else if constexpr (type == VariableType::BOOLEAN) {
    return detail::lookup_bool_from_raw(raw);
  } else if constexpr (type == VariableType::INT) {
    return detail::lookup_uint64_from_raw(raw);
  } else if constexpr (type == VariableType::DECIMAL) {
    return detail::lookup_double_from_raw(raw);
  } else {
    static_assert(detail::unsupported_variable_type_v<type>,
                  "Unsupported environment variable type");
  }
}

#undef VARIABLE_SPEC_WITH_COMMA
#undef VARIABLE_TRAITS_VALUE
#undef QUOTED
#undef QUOTED_IMPL
#undef VARIABLE_ENUM_VALUE

// Return the metadata for the specified environment `variable`.
const VariableSpec &spec(Variable variable);

// Return the name of the specified environment `variable`.
StringView name(Variable variable);

std::string to_json();

}  // namespace environment
}  // namespace tracing
}  // namespace datadog
