#include <yaml-cpp/yaml.h>

#include <algorithm>
#include <map>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include "stable_config_loader_internal.h"

namespace datadog::tracing::stable_config_internal {
namespace {

struct Selector {
  std::string origin;
  std::string key;
  bool has_key = false;
  std::string operation;
  std::vector<std::string> matches;
};

struct Rule {
  std::vector<Selector> selectors;
  std::vector<std::pair<std::string, std::string>> configuration;
};

struct ParsedConfig {
  std::string config_id;
  std::vector<std::pair<std::string, std::string>> defaults;
  std::map<std::string, std::string> tags;
  std::vector<Rule> rules;
};

std::string scalar(const YAML::Node& node, const char* name) {
  if (!node || !node.IsScalar()) {
    throw std::runtime_error(std::string{name} + " must be a scalar");
  }
  return node.Scalar();
}

std::vector<std::pair<std::string, std::string>> parse_map(
    const YAML::Node& node, const char* name) {
  if (!node || !node.IsMap()) {
    throw std::runtime_error(std::string{name} + " must be a map");
  }
  std::vector<std::pair<std::string, std::string>> entries;
  for (YAML::const_iterator entry = node.begin(); entry != node.end();
       ++entry) {
    if (!entry->first.IsScalar()) continue;
    entries.emplace_back(entry->first.Scalar(),
                         scalar(entry->second, "configuration value"));
  }
  return entries;
}

Selector parse_selector(const YAML::Node& node) {
  if (!node.IsMap()) throw std::runtime_error("selector must be a map");
  Selector result;
  result.origin = scalar(node["origin"], "selector origin");
  if (result.origin != "language" && result.origin != "process_arguments" &&
      result.origin != "environment_variables" && result.origin != "tags") {
    throw std::runtime_error("unknown selector origin: " + result.origin);
  }
  if (const YAML::Node key = node["key"]; key && !key.IsNull()) {
    result.key = scalar(key, "selector key");
    result.has_key = true;
  }
  result.operation = scalar(node["operator"], "selector operator");
  if (result.operation != "exists" && result.operation != "equals" &&
      result.operation != "prefix_matches" &&
      result.operation != "suffix_matches") {
    throw std::runtime_error("unknown selector operator: " + result.operation);
  }
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
  const std::string text = content.empty()
                               ? std::string{}
                               : std::string(content.data(), content.size());
  const YAML::Node document = YAML::Load(text);
  ParsedConfig result;
  if (!document || document.IsNull()) return result;
  if (!document.IsMap()) throw std::runtime_error("root must be a map");
  if (const YAML::Node id = document["config_id"]; id && !id.IsNull()) {
    result.config_id = scalar(id, "config_id");
  }
  if (const YAML::Node defaults = document["apm_configuration_default"];
      defaults) {
    result.defaults = parse_map(defaults, "apm_configuration_default");
  }
  if (const YAML::Node tags = document["tags"]; tags) {
    if (!tags.IsMap()) throw std::runtime_error("tags must be a map");
    for (YAML::const_iterator tag = tags.begin(); tag != tags.end(); ++tag) {
      result.tags.insert_or_assign(scalar(tag->first, "tag key"),
                                   scalar(tag->second, "tag value"));
    }
  }
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

std::map<std::string, std::string> split_entries(
    const std::vector<std::string>& entries, bool include_without_equals) {
  std::map<std::string, std::string> result;
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

bool selector_matches(const Selector& selector, const ParsedConfig& source,
                      const ProcessInfo& process,
                      const std::map<std::string, std::string>& args,
                      const std::map<std::string, std::string>& environment) {
  if (selector.origin == "language")
    return string_match(selector, process.language);
  if (selector.origin == "tags") {
    if (!selector.has_key) return false;
    const auto found = source.tags.find(selector.key);
    return found != source.tags.end() && string_match(selector, found->second);
  }
  if (selector.has_key) {
    const std::map<std::string, std::string>& values =
        selector.origin == "process_arguments" ? args : environment;
    const auto found = values.find(selector.key);
    return found != values.end() && string_match(selector, found->second);
  }
  const std::vector<std::string>& values =
      selector.origin == "process_arguments" ? process.args
                                             : process.environment;
  for (const std::string& value : values) {
    if (string_match(selector, value)) return true;
  }
  return false;
}

std::string trim(std::string value) {
  const std::size_t start = value.find_first_not_of(" \t\n\r");
  if (start == std::string::npos) return "";
  const std::size_t end = value.find_last_not_of(" \t\n\r");
  return value.substr(start, end - start + 1);
}

std::string resolve_template_variable(
    const std::string& variable, const ParsedConfig& source,
    const ProcessInfo& process, const std::map<std::string, std::string>& args,
    const std::map<std::string, std::string>& environment) {
  const std::size_t bracket = variable.find('[');
  const std::string name = variable.substr(0, bracket);
  if (name == "language") return process.language;
  if (bracket == std::string::npos) return "UNDEFINED";
  const std::size_t end = variable.find(']', bracket + 1);
  if (end == std::string::npos) return "UNDEFINED";

  const std::map<std::string, std::string>* values = nullptr;
  if (name == "tags") values = &source.tags;
  if (name == "process_arguments") values = &args;
  if (name == "environment_variables") values = &environment;
  if (!values) return "UNDEFINED";

  const std::string key = trim(variable.substr(bracket + 1, end - bracket - 1));
  const auto found = values->find(key);
  return found == values->end() ? "UNDEFINED" : found->second;
}

std::string template_value(
    const std::string& input, const ParsedConfig& source,
    const ProcessInfo& process, const std::map<std::string, std::string>& args,
    const std::map<std::string, std::string>& environment) {
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
    output +=
        resolve_template_variable(variable, source, process, args, environment);
    cursor = close + 2;
  }
}

void apply_source(StableConfig& result, const ParsedConfig& source,
                  StableConfigSource origin, const ProcessInfo& process) {
  for (const std::pair<std::string, std::string>& entry : source.defaults) {
    result.set(entry.first, {entry.second, origin, source.config_id});
  }
  const std::map<std::string, std::string> args =
      split_entries(process.args, false);
  const std::map<std::string, std::string> environment =
      split_entries(process.environment, true);
  for (const Rule& rule : source.rules) {
    const bool matches = std::all_of(
        rule.selectors.begin(), rule.selectors.end(),
        [&](const Selector& selector) {
          return selector_matches(selector, source, process, args, environment);
        });
    if (!matches) continue;
    for (const std::pair<std::string, std::string>& entry :
         rule.configuration) {
      result.set(entry.first, {template_value(entry.second, source, process,
                                              args, environment),
                               origin, source.config_id});
    }
    break;
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
