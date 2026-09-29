#include <yaml-cpp/yaml.h>

#include <algorithm>
#include <initializer_list>
#include <map>
#include <stdexcept>
#include <string>
#include <vector>

#include "stable_config_loader_internal.h"

namespace datadog::tracing::stable_config_internal {
namespace {

using Map = std::map<std::string, std::string>;

struct Selector {
  std::string origin;
  Optional<std::string> key;
  std::string operation;
  std::vector<std::string> matches;
};

struct Rule {
  std::vector<Selector> selectors;
  Map configuration;
};

struct ParsedConfig {
  std::string config_id;
  Map defaults;
  Map tags;
  std::vector<Rule> rules;
};

// Values a selector or template can read by key.
struct Context {
  const ProcessInfo& process;
  const Map& tags;
  Map args;
  Map environment;

  const Map* values(const std::string& origin) const {
    if (origin == "tags") return &tags;
    if (origin == "process_arguments") return &args;
    if (origin == "environment_variables") return &environment;
    return nullptr;
  }
};

std::string scalar(const YAML::Node& node, const char* name) {
  if (!node || !node.IsScalar()) {
    throw std::runtime_error(std::string{name} + " must be a scalar");
  }
  return node.Scalar();
}

std::string one_of(const YAML::Node& node, const char* name,
                   std::initializer_list<StringView> allowed) {
  std::string value = scalar(node, name);
  if (std::find(allowed.begin(), allowed.end(), value) == allowed.end()) {
    throw std::runtime_error("unknown " + std::string{name} + ": " + value);
  }
  return value;
}

// Later duplicate keys win. Non-scalar keys are skipped.
Map parse_map(const YAML::Node& node, const char* name) {
  if (!node || !node.IsMap()) {
    throw std::runtime_error(std::string{name} + " must be a map");
  }
  Map entries;
  for (YAML::const_iterator entry = node.begin(); entry != node.end();
       ++entry) {
    if (!entry->first.IsScalar()) continue;
    entries.insert_or_assign(entry->first.Scalar(),
                             scalar(entry->second, "map value"));
  }
  return entries;
}

Selector parse_selector(const YAML::Node& node) {
  if (!node.IsMap()) throw std::runtime_error("selector must be a map");
  Selector result;
  result.origin = one_of(
      node["origin"], "selector origin",
      {"language", "process_arguments", "environment_variables", "tags"});
  if (const YAML::Node key = node["key"]; key && !key.IsNull()) {
    result.key = scalar(key, "selector key");
  }
  result.operation =
      one_of(node["operator"], "selector operator",
             {"exists", "equals", "prefix_matches", "suffix_matches"});
  if (result.operation != "exists") {
    const YAML::Node matches = node["matches"];
    if (!matches || !matches.IsSequence()) {
      throw std::runtime_error("selector matches must be a sequence");
    }
    for (const YAML::Node& match : matches) {
      result.matches.push_back(scalar(match, "selector match"));
    }
  }
  return result;
}

Rule parse_rule(const YAML::Node& node) {
  if (!node.IsMap()) throw std::runtime_error("rule must be a map");
  const YAML::Node selectors = node["selectors"];
  if (!selectors || !selectors.IsSequence()) {
    throw std::runtime_error("rule selectors must be a sequence");
  }
  Rule rule;
  for (const YAML::Node& selector : selectors) {
    rule.selectors.push_back(parse_selector(selector));
  }
  rule.configuration = parse_map(node["configuration"], "rule configuration");
  return rule;
}

ParsedConfig parse_yaml(StringView content) {
  const YAML::Node document = YAML::Load(std::string{content});
  ParsedConfig result;
  if (!document || document.IsNull()) return result;
  if (!document.IsMap()) throw std::runtime_error("root must be a map");
  if (const YAML::Node id = document["config_id"]; id && !id.IsNull()) {
    result.config_id = scalar(id, "config_id");
  }
  if (document["apm_configuration_default"]) {
    result.defaults = parse_map(document["apm_configuration_default"],
                                "apm_configuration_default");
  }
  if (document["tags"]) result.tags = parse_map(document["tags"], "tags");
  if (const YAML::Node rules = document["rules"]; rules) {
    if (!rules.IsSequence())
      throw std::runtime_error("rules must be a sequence");
    for (const YAML::Node& item : rules) {
      result.rules.push_back(parse_rule(item));
    }
  }
  return result;
}

bool valid_utf8(const std::string& value) {
  for (std::size_t index = 0; index < value.size();) {
    const unsigned char first = static_cast<unsigned char>(value[index]);
    if (first < 0x80) {
      ++index;
      continue;
    }
    const std::size_t length = first >= 0xf0 ? 4 : first >= 0xe0 ? 3 : 2;
    if (first < 0xc2 || first > 0xf4 || index + length > value.size())
      return false;
    for (std::size_t offset = 1; offset < length; ++offset) {
      const unsigned char next =
          static_cast<unsigned char>(value[index + offset]);
      if ((next & 0xc0) != 0x80) return false;
    }
    const unsigned char second = static_cast<unsigned char>(value[index + 1]);
    if ((first == 0xe0 && second < 0xa0) || (first == 0xed && second >= 0xa0) ||
        (first == 0xf0 && second < 0x90) || (first == 0xf4 && second >= 0x90))
      return false;
    index += length;
  }
  return true;
}

Map split_entries(const std::vector<std::string>& entries,
                  bool include_without_equals) {
  Map result;
  for (const std::string& entry : entries) {
    if (!valid_utf8(entry)) continue;
    const std::size_t equal = entry.find('=');
    if (equal == std::string::npos && !include_without_equals) continue;
    result.insert_or_assign(
        entry.substr(0, equal),
        equal == std::string::npos ? "" : entry.substr(equal + 1));
  }
  return result;
}

bool string_match(const Selector& selector, const std::string& value) {
  if (selector.operation == "exists") return true;
  for (const std::string& match : selector.matches) {
    if (selector.operation == "equals" && value == match) return true;
    if (selector.operation == "prefix_matches" &&
        value.compare(0, match.size(), match) == 0)
      return true;
    if (selector.operation == "suffix_matches" &&
        value.size() >= match.size() &&
        value.compare(value.size() - match.size(), match.size(), match) == 0) {
      return true;
    }
  }
  return false;
}

bool selector_matches(const Selector& selector, const Context& context) {
  if (selector.origin == "language") {
    return string_match(selector, context.process.language);
  }
  if (selector.key) {
    const Map& values = *context.values(selector.origin);
    const auto found = values.find(*selector.key);
    return found != values.end() && string_match(selector, found->second);
  }
  if (selector.origin == "tags") return false;
  const std::vector<std::string>& values =
      selector.origin == "process_arguments" ? context.process.args
                                             : context.process.environment;
  return std::any_of(values.begin(), values.end(), [&](const std::string& v) {
    return string_match(selector, v);
  });
}

std::string trim(std::string value) {
  const std::size_t start = value.find_first_not_of(" \t\n\r");
  if (start == std::string::npos) return "";
  const std::size_t end = value.find_last_not_of(" \t\n\r");
  return value.substr(start, end - start + 1);
}

// Resolve `language` or `origin[key]`. Unknown values become "UNDEFINED".
std::string resolve_template_variable(const std::string& variable,
                                      const Context& context) {
  const std::size_t bracket = variable.find('[');
  const std::string name = variable.substr(0, bracket);
  if (name == "language") return context.process.language;
  const std::size_t end = variable.find(']', bracket);
  const Map* values = context.values(name);
  if (end == std::string::npos || !values) return "UNDEFINED";
  const auto found =
      values->find(trim(variable.substr(bracket + 1, end - bracket - 1)));
  return found == values->end() ? "UNDEFINED" : found->second;
}

std::string template_value(const std::string& input, const Context& context) {
  std::string output;
  std::size_t cursor = 0;
  while (true) {
    const std::size_t open = input.find("{{", cursor);
    if (open == std::string::npos) return output + input.substr(cursor);
    output += input.substr(cursor, open - cursor);
    const std::size_t close = input.find("}}", open + 2);
    if (close == std::string::npos)
      throw std::runtime_error("unterminated template in config");
    const std::string variable = trim(input.substr(open + 2, close - open - 2));
    output += resolve_template_variable(variable, context);
    cursor = close + 2;
  }
}

// Apply the defaults, then the first rule that matches.
void apply_source(StableConfig& result, const ParsedConfig& source,
                  StableConfigSource origin, const ProcessInfo& process) {
  for (const auto& [name, value] : source.defaults) {
    result.set(name, {value, origin, source.config_id});
  }
  const Context context{process, source.tags,
                        split_entries(process.args, false),
                        split_entries(process.environment, true)};
  for (const Rule& rule : source.rules) {
    if (!std::all_of(rule.selectors.begin(), rule.selectors.end(),
                     [&](const Selector& selector) {
                       return selector_matches(selector, context);
                     })) {
      continue;
    }
    for (const auto& [name, value] : rule.configuration) {
      result.set(name,
                 {template_value(value, context), origin, source.config_id});
    }
    return;
  }
}

}  // namespace

Expected<StableConfig> load_yaml(StringView local_yaml, StringView fleet_yaml,
                                 const ProcessInfo& process) {
  try {
    if (!valid_utf8(process.language)) {
      throw std::runtime_error("language is not UTF-8");
    }
    const ParsedConfig local = parse_yaml(local_yaml);
    const ParsedConfig fleet = parse_yaml(fleet_yaml);
    StableConfig result;
    apply_source(result, local, StableConfigSource::LOCAL, process);
    apply_source(result, fleet, StableConfigSource::FLEET, process);
    return result;
  } catch (const std::exception& error) {
    return Error{Error::OTHER, "Unable to load stable configuration: " +
                                   std::string{error.what()}};
  }
}

}  // namespace datadog::tracing::stable_config_internal
