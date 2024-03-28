#pragma once

#include <string>
#include <unordered_map>

#include "propagation_style.h"
#include "string_view.h"

namespace datadog {
namespace tracing {

// Return a string representation of the specified boolean `value`.
// The result is "true" for `true` and "false" for `false`.
std::string to_string(bool b);

// Converts a double value to a string
std::string to_string(double d, size_t precision);

// Joins elements of a vector into a single string with a specified separator
std::string join(const std::vector<StringView>& values, StringView separator);
std::string join(const std::vector<std::string>& values, StringView separator);

// Joins propagation styles into a single comma-separated string
std::string join_propagation_styles(const std::vector<PropagationStyle>&);

// Joins key-value pairs into a single comma-separated string
std::string join_tags(
    const std::unordered_map<std::string, std::string>& values);

StringView trim(StringView);

}  // namespace tracing
}  // namespace datadog
