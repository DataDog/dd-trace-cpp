#include "endpoint_guessing.h"

#include <cstdint>

namespace datadog::tracing {

namespace {

constexpr size_t MAX_COMPONENTS = 8;

inline constexpr bool is_digit(char c) noexcept { return c >= '0' && c <= '9'; }
inline constexpr bool is_hex_alpha(char c) noexcept {
  return (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F');
}
inline constexpr bool is_delim(char c) noexcept {
  return c == '.' || c == '_' || c == '-';
}
inline constexpr bool is_str_special(char c) noexcept {
  return c == '%' || c == '&' || c == '\'' || c == '(' || c == ')' ||
         c == '*' || c == '+' || c == ',' || c == ':' || c == '=' || c == '@';
}

/*
clang-format off
{param:int}     [1-9][0-9]+                   len≥2, digits only, first 1–9
{param:int_id}  (?=.*[0-9])[0-9._-]{3,}       len≥3, [0-9._-], must contain digit
{param:hex}     (?=.*[0-9])[A-Fa-f0-9]{6,}    len≥6, hex digits, must contain decimal digit
{param:hex_id}  (?=.*[0-9])[A-Fa-f0-9._-]{6,} len≥6, hex+._-, must contain decimal digit
{param:str}     .{20,}|.*[%&'()*+,:=@].*      any chars, valid if len≥20 or contains special
clang-format on
*/
enum component_type : std::uint8_t {
  none = 0,
  is_int = 1 << 0,
  is_int_id = 1 << 1,
  is_hex = 1 << 2,
  is_hex_id = 1 << 3,
  is_str = 1 << 4,
};

std::string_view to_string(component_type type) noexcept {
  switch (type) {
    case component_type::is_int:
      return "{param:int}";
    case component_type::is_int_id:
      return "{param:int_id}";
    case component_type::is_hex:
      return "{param:hex}";
    case component_type::is_hex_id:
      return "{param:hex_id}";
    case component_type::is_str:
      return "{param:str}";
    default:
      return "";
  }
}

inline uint8_t bool2mask(bool x) noexcept {
  return static_cast<uint8_t>(-int{x});  // 0 -> 0x00, 1 -> 0xFF
}

component_type component_replacement(std::string_view path) noexcept {
  // viable_components is a bitset of the component types not yet excluded
  std::uint8_t viable_components = 0x1F;  // (is_str << 1) - 1
  bool found_special_char = false;
  bool found_digit = false;

  if (path.size() < 2) {
    viable_components &= ~(component_type::is_int | component_type::is_int_id |
                           component_type::is_hex | component_type::is_hex_id);
  } else if (path.size() < 3) {
    viable_components &= ~(component_type::is_int_id | component_type::is_hex |
                           component_type::is_hex_id);
  } else if (path.size() < 6) {
    viable_components &= ~(component_type::is_hex | component_type::is_hex_id);
  }

  // is_int does not allow a leading 0
  if (!path.empty() && path[0] == '0') {
    viable_components &= ~component_type::is_int;
  }

  for (std::size_t i = 0; i < path.size(); ++i) {
    char c = path[i];
    found_special_char = found_special_char || is_str_special(c);
    found_digit = found_digit || is_digit(c);

    std::uint8_t digit_mask =
        bool2mask(is_digit(c)) &
        (component_type::is_int | component_type::is_int_id |
         component_type::is_hex | component_type::is_hex_id);

    std::uint8_t hex_alpha_mask =
        bool2mask(is_hex_alpha(c)) &
        (component_type::is_hex | component_type::is_hex_id);

    std::uint8_t delimiter_mask =
        bool2mask(is_delim(c)) &
        (component_type::is_int_id | component_type::is_hex_id);

    viable_components &=
        (digit_mask | hex_alpha_mask | delimiter_mask | component_type::is_str);
  }

  // is_str requires a special char or a size >= 20
  viable_components &= ~component_type::is_str |
                       bool2mask(found_special_char || (path.size() >= 20));
  // hex, and hex_id require a digit
  viable_components &= ~(component_type::is_hex | component_type::is_hex_id) |
                       bool2mask(found_digit);

  if (viable_components == 0) {
    return component_type::none;
  }

  // c++20: use std::countr_zero
  std::uint8_t lsb = static_cast<std::uint8_t>(
      viable_components &
      static_cast<std::uint8_t>(-static_cast<int8_t>(viable_components)));
  return static_cast<component_type>(lsb);
}
}  // namespace

std::string guess_endpoint(std::string_view orig_path) {
  auto path = orig_path;

  // remove the query string if any
  auto query_pos = path.find('?');
  if (query_pos != std::string_view::npos) {
    path = path.substr(0, query_pos);
  }

  if (path.empty() || path.front() != '/') {
    return "/";
  }

  std::string result{};
  size_t component_count = 0;

  path.remove_prefix(1);
  while (!path.empty()) {
    auto slash_pos = path.find('/');

    std::string_view component = path.substr(0, slash_pos);

    // remove current component from the path
    if (slash_pos == std::string_view::npos) {
      path = std::string_view{};
    } else {
      path.remove_prefix(slash_pos + 1);
    }

    if (component.empty()) {
      continue;
    }

    result.append("/");

    // replace the literal component with the appropriate placeholder
    // (if it matches one of the patterns)
    auto type = component_replacement(component);
    if (type == component_type::none) {
      result.append(component);
    } else {
      result.append(to_string(type));
    }
    if (++component_count >= MAX_COMPONENTS) {
      break;
    }
  }

  if (result.empty()) {
    return "/";
  }

  return result;
}

}  // namespace datadog::tracing
