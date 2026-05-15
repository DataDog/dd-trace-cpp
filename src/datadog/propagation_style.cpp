#include <datadog/error.h>
#include <datadog/propagation_style.h>

#include <algorithm>
#include <cassert>
#include <string>

#include "json.hpp"
#include "parse_util.h"
#include "string_util.h"

namespace datadog {
namespace tracing {

StringView to_string_view(PropagationStyle style) {
  // Note: Make sure that these strings are consistent (modulo case) with
  // `parse_propagation_styles` in `tracer_config.cpp`.
  switch (style) {
    case PropagationStyle::DATADOG:
      return "Datadog";
    case PropagationStyle::B3:
      return "B3";
    case PropagationStyle::W3C:
      return "tracecontext";  // for compatibility with OpenTelemetry
    case PropagationStyle::BAGGAGE:
      return "baggage";
    default:
      assert(style == PropagationStyle::NONE);
      return "none";
  }
}

nlohmann::json to_json(PropagationStyle style) { return to_string_view(style); }

nlohmann::json to_json(const std::vector<PropagationStyle>& styles) {
  std::vector<nlohmann::json> styles_json;
  for (const auto style : styles) {
    styles_json.push_back(to_json(style));
  }
  return styles_json;
}

Optional<PropagationStyle> parse_propagation_style(StringView text) {
  auto token = std::string{text};
  to_lower(token);

  if (token == "datadog") {
    return PropagationStyle::DATADOG;
  } else if (token == "b3" || token == "b3multi") {
    return PropagationStyle::B3;
  } else if (token == "tracecontext") {
    return PropagationStyle::W3C;
  } else if (token == "none") {
    return PropagationStyle::NONE;
  } else if (token == "baggage") {
    return PropagationStyle::BAGGAGE;
  }

  return nullopt;
}

Expected<std::vector<PropagationStyle>> parse_propagation_styles(
    StringView input) {
  std::vector<PropagationStyle> styles;

  const auto last_is_duplicate = [&]() -> Optional<Error> {
    assert(!styles.empty());

    const auto dupe =
        std::find(styles.begin(), styles.end() - 1, styles.back());
    if (dupe == styles.end() - 1) {
      return nullopt;  // no duplicate
    }

    std::string message;
    message += "The propagation style ";
    message += std::string(to_string_view(styles.back()));
    message += " is duplicated in: ";
    append(message, input);
    return Error{Error::DUPLICATE_PROPAGATION_STYLE, std::move(message)};
  };

  // Style names are separated by spaces, or a comma, or some combination.
  for (const StringView& item : parse_list(input)) {
    if (const auto style = parse_propagation_style(item)) {
      styles.push_back(*style);
    } else {
      std::string message;
      message += "Unsupported propagation style \"";
      append(message, item);
      message += "\" in list \"";
      append(message, input);
      message +=
          "\".  The following styles are supported: Datadog, B3, tracecontext.";
      return Error{Error::UNKNOWN_PROPAGATION_STYLE, std::move(message)};
    }

    if (auto maybe_error = last_is_duplicate()) {
      return *maybe_error;
    }
  }

  return styles;
}

}  // namespace tracing
}  // namespace datadog
