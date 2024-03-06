#pragma once

#include <string>

#include "propagation_style.h"

namespace datadog {
namespace tracing {

// Converts a boolean value to a string
// The resulted string is either `true` or `false`
std::string to_string(bool b);

// Converts a double value to a string
std::string to_string(double d, size_t precision);

// Joins elements of a vector into a single string with a specified separator
std::string join(const std::vector<StringView>& values,
                 const char* const separator);

// Joins propagation styles into a single comma-separated string
std::string join_propagation_styles(const std::vector<PropagationStyle>&);

// Joins key-value pairs into a single comma-separated string
std::string join_tags(
    const std::unordered_map<std::string, std::string>& values);

}  // namespace tracing
}  // namespace datadog
