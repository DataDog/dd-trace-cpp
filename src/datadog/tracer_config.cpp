#include "tracer_config.h"

#include <cctype>
#include <regex>
#include <string>
#include <string_view>
#include <unordered_map>

#include "cerr_logger.h"
#include "datadog_agent.h"
#include "environment.h"

namespace datadog {
namespace tracing {
namespace {

void to_lower(std::string &text) {
  std::transform(text.begin(), text.end(), text.begin(),
                 [](unsigned char ch) { return std::tolower(ch); });
}

Expected<PropagationStyles> parse_propagation_styles(std::string_view input) {
  PropagationStyles styles{false, false, false};
  if (std::all_of(input.begin(), input.end(),
                  [](unsigned char ch) { return std::isspace(ch); })) {
    return styles;
  }

  // Style names are separated by spaces, or a comma, or some combination.
  std::regex separator{R"pcre(\s*,\s*|\s+)pcre"};
  const auto begin =
      std::cregex_token_iterator{input.begin(), input.end(), separator, -1};
  const auto end = std::cregex_token_iterator{};
  for (auto iter = begin; iter != end; ++iter) {
    std::string token = *iter;
    to_lower(token);
    if (token == "datadog") {
      styles.datadog = true;
    } else if (token == "b3") {
      styles.b3 = true;
    } else if (token == "w3c") {
      styles.w3c = true;
    } else {
      std::string message;
      message += "Unsupported propagation style \"";
      message += token;
      message += "\" in list \"";
      message += input;
      message += "\".  The following styles are supported: Datadog, B3, W3C.";
      return Error{Error::UNKNOWN_PROPAGATION_STYLE, std::move(message)};
    }
  }

  return styles;
}

Expected<std::unordered_map<std::string, std::string>> parse_tags(
    std::string_view input) {
  std::unordered_map<std::string, std::string> tags;
  if (std::all_of(input.begin(), input.end(),
                  [](unsigned char ch) { return std::isspace(ch); })) {
    return tags;
  }

  // Tags are separated by spaces, or a comma, or some combination.
  // Within a tag, the key and value are separated by a colon (":").
  // This regex just separates the tags from each other.
  std::regex separator{R"pcre(\s*,\s*|\s+)pcre"};
  const auto begin =
      std::cregex_token_iterator{input.begin(), input.end(), separator, -1};
  const auto end = std::cregex_token_iterator{};
  for (auto iter = begin; iter != end; ++iter) {
    std::string token = *iter;
    const auto separator = std::find(token.begin(), token.end(), ':');
    if (separator == token.end()) {
      std::string message;
      message += "Unable to parse a key/value from the tag text \"";
      message += token;
      message +=
          "\" because it does not contain the separator character \":\".  "
          "Error occurred in list of tags \"";
      message += input;
      message += "\".";
      return Error{Error::TAG_MISSING_SEPARATOR, std::move(message)};
    }
    std::string key{token.begin(), separator};
    std::string value{separator + 1, token.end()};
    // If there are duplicate values, then the last one wins.
    tags.insert_or_assign(std::move(key), std::move(value));
  }

  return tags;
}

}  // namespace

Expected<FinalizedTracerConfig> finalize_config(const TracerConfig &config) {
  // TODO: environment variables, validation, and other fun.
  FinalizedTracerConfig result;

  if (config.defaults.service.empty()) {
    return Error{Error::SERVICE_NAME_REQUIRED, "Service name is required."};
  }

  result.defaults = config.defaults;

  if (auto tags_env = lookup(environment::DD_TAGS)) {
    auto tags = parse_tags(*tags_env);
    if (auto *error = tags.if_error()) {
      std::string prefix;
      prefix += "Unable to parse ";
      prefix += name(environment::DD_TAGS);
      prefix += " environment variable: ";
      return error->with_prefix(prefix);
    }
    result.defaults.tags = std::move(*tags);
  }

  if (config.logger) {
    result.logger = config.logger;
  } else {
    result.logger = std::make_shared<CerrLogger>();
  }

  if (!config.collector) {
    auto finalized = finalize_config(config.agent);
    if (auto *error = finalized.if_error()) {
      return std::move(*error);
    }
    result.collector =
        std::make_shared<DatadogAgent>(*finalized, result.logger);
  } else {
    result.collector = config.collector;
  }

  if (auto trace_sampler_config = finalize_config(config.trace_sampler)) {
    result.trace_sampler = std::move(*trace_sampler_config);
  } else {
    return std::move(trace_sampler_config.error());
  }

  if (auto span_sampler_config = finalize_config(config.span_sampler)) {
    result.span_sampler = std::move(*span_sampler_config);
  } else {
    return std::move(span_sampler_config.error());
  }

  result.extraction_styles = config.extraction_styles;
  if (auto styles_env = lookup(environment::DD_PROPAGATION_STYLE_EXTRACT)) {
    auto styles = parse_propagation_styles(*styles_env);
    if (auto *error = styles.if_error()) {
      std::string prefix;
      prefix += "Unable to parse ";
      prefix += name(environment::DD_PROPAGATION_STYLE_EXTRACT);
      prefix += " environment variable: ";
      return error->with_prefix(prefix);
    }
    result.extraction_styles = *styles;
  }

  result.injection_styles = config.injection_styles;
  if (auto styles_env = lookup(environment::DD_PROPAGATION_STYLE_INJECT)) {
    auto styles = parse_propagation_styles(*styles_env);
    if (auto *error = styles.if_error()) {
      std::string prefix;
      prefix += "Unable to parse ";
      prefix += name(environment::DD_PROPAGATION_STYLE_INJECT);
      prefix += " environment variable: ";
      return error->with_prefix(prefix);
    }
    result.injection_styles = *styles;
  }

  // TODO: implement the other styles
  const auto not_implemented = [](std::string_view style,
                                  std::string_view operation) {
    std::string message;
    message += "The ";
    message += style;
    message += ' ';
    message += operation;
    message += " style is not yet supported. Only datadog is supported.";
    return Error{Error::NOT_IMPLEMENTED, std::move(message)};
  };

  if (result.extraction_styles.b3) {
    return not_implemented("b3", "extraction");
  } else if (result.extraction_styles.w3c) {
    return not_implemented("w3c", "extraction");
  } else if (result.injection_styles.b3) {
    return not_implemented("b3", "injection");
  } else if (result.injection_styles.w3c) {
    return not_implemented("w3c", "injection");
  } else if (!result.extraction_styles.datadog) {
    return Error{Error::MISSING_SPAN_EXTRACTION_STYLE,
                 "At least one extraction style must be specified."};
  } else if (!result.injection_styles.datadog) {
    return Error{Error::MISSING_SPAN_INJECTION_STYLE,
                 "At least one injection style must be specified."};
  }

  result.report_hostname = config.report_hostname;
  result.tags_header_size = config.tags_header_size;

  return result;
}

}  // namespace tracing
}  // namespace datadog
