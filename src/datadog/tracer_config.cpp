#include "tracer_config.h"

#include <algorithm>
#include <cctype>
#include <cstddef>
#include <string>
#include "string_view.h"
#include <unordered_map>
#include <vector>

#include "cerr_logger.h"
#include "datadog_agent.h"
#include "environment.h"
#include "null_collector.h"
#include "parse_util.h"

namespace datadog {
namespace tracing {
namespace {

void to_lower(std::string &text) {
  std::transform(text.begin(), text.end(), text.begin(),
                 [](unsigned char ch) { return std::tolower(ch); });
}

bool falsy(StringView text) {
  auto lower = std::string{text};
  to_lower(lower);
  return lower == "0" || lower == "false" || lower == "no";
}

// List items are separated by an optional comma (",") and any amount of
// whitespace.
// Leading and trailing whitespace is ignored.
std::vector<StringView> parse_list(StringView input) {
  using uchar = unsigned char;

  input = strip(input);
  std::vector<StringView> items;
  if (input.empty()) {
    return items;
  }

  const char *const end = input.end();

  const char *current = input.begin();
  const char *begin_delim;
  do {
    const char *begin_item =
        std::find_if(current, end, [](uchar ch) { return !std::isspace(ch); });
    begin_delim = std::find_if(begin_item, end, [](uchar ch) {
      return std::isspace(ch) || ch == ',';
    });

    items.emplace_back(begin_item, std::size_t(begin_delim - begin_item));

    const char *end_delim = std::find_if(
        begin_delim, end, [](uchar ch) { return !std::isspace(ch); });

    if (end_delim != end && *end_delim == ',') {
      ++end_delim;
    }

    current = end_delim;
  } while (begin_delim != end);

  return items;
}

Expected<PropagationStyles> parse_propagation_styles(StringView input) {
  PropagationStyles styles{false, false};

  // Style names are separated by spaces, or a comma, or some combination.
  for (const StringView &item : parse_list(input)) {
    auto token = std::string(item);
    to_lower(token);
    if (token == "datadog") {
      styles.datadog = true;
    } else if (token == "b3") {
      styles.b3 = true;
    } else {
      std::string message;
      message += "Unsupported propagation style \"";
      message += token;
      message += "\" in list \"";
      message += input;
      message += "\".  The following styles are supported: Datadog, B3.";
      return Error{Error::UNKNOWN_PROPAGATION_STYLE, std::move(message)};
    }
  }

  return styles;
}

Expected<std::unordered_map<std::string, std::string>> parse_tags(
    StringView input) {
  std::unordered_map<std::string, std::string> tags;

  // Within a tag, the key and value are separated by a colon (":").
  for (const StringView &token : parse_list(input)) {
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
  FinalizedTracerConfig result;

  result.defaults = config.defaults;

  if (auto service_env = lookup(environment::DD_SERVICE)) {
    result.defaults.service = *service_env;
  }
  if (result.defaults.service.empty()) {
    return Error{Error::SERVICE_NAME_REQUIRED, "Service name is required."};
  }

  if (auto environment_env = lookup(environment::DD_ENV)) {
    result.defaults.environment = *environment_env;
  }
  if (auto version_env = lookup(environment::DD_VERSION)) {
    result.defaults.version = *version_env;
  }

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

  result.log_on_startup = config.log_on_startup;
  if (auto startup_env = lookup(environment::DD_TRACE_STARTUP_LOGS)) {
    result.log_on_startup = !falsy(*startup_env);
  }

  bool report_traces = config.report_traces;
  if (auto enabled_env = lookup(environment::DD_TRACE_ENABLED)) {
    report_traces = !falsy(*enabled_env);
  }

  if (!report_traces) {
    result.collector = std::make_shared<NullCollector>();
  } else if (!config.collector) {
    auto finalized = finalize_config(config.agent, result.logger);
    if (auto *error = finalized.if_error()) {
      return std::move(*error);
    }
    result.collector = *finalized;
  } else {
    result.collector = config.collector;
  }

  if (auto trace_sampler_config = finalize_config(config.trace_sampler)) {
    result.trace_sampler = std::move(*trace_sampler_config);
  } else {
    return std::move(trace_sampler_config.error());
  }

  if (auto span_sampler_config =
          finalize_config(config.span_sampler, *result.logger)) {
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

  if (!result.extraction_styles.datadog && !result.extraction_styles.b3) {
    return Error{Error::MISSING_SPAN_EXTRACTION_STYLE,
                 "At least one extraction style must be specified."};
  } else if (!result.injection_styles.datadog && !result.injection_styles.b3) {
    return Error{Error::MISSING_SPAN_INJECTION_STYLE,
                 "At least one injection style must be specified."};
  }

  result.report_hostname = config.report_hostname;
  result.tags_header_size = config.tags_header_size;

  return result;
}

}  // namespace tracing
}  // namespace datadog
