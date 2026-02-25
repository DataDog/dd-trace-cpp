#include <algorithm>
#include <array>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <optional>
#include <regex>
#include <set>
#include <stdexcept>
#include <string>
#include <vector>

#include "json.hpp"

namespace fs = std::filesystem;
using nlohmann::json;

namespace {

const std::vector<std::string> kScanDirs = {"include", "src", "test", "examples",
                                            "fuzz"};
const std::set<std::string> kScanSuffixes = {".h", ".hh", ".hpp", ".c",
                                             ".cc", ".cpp", ".cxx"};
const std::vector<std::string> kDynamicScanDirs = {"include", "src"};
const std::set<std::string> kAllowedDynamicGetenvPaths = {
    "include/datadog/environment.h",
    "src/datadog/environment.cpp",
};

const std::regex kEnvNameRe("^(?:DD|OTEL)_[A-Z0-9_]+$");
const std::regex kEnvEnumRe("\\b(?:environment|env)::((?:DD|OTEL)_[A-Z0-9_]+)\\b");
const std::regex kGetenvRe(
    "\\b(?:std::)?getenv\\s*\\(\\s*\"((?:DD|OTEL)_[A-Z0-9_]+)\"\\s*\\)");
const std::regex kGetenvCallRe("\\b(?:std::)?getenv\\s*\\(\\s*([^)]+?)\\s*\\)");
const std::regex kSetenvRe(
    "\\b(?:setenv|setenv_s|_putenv_s?)\\s*\\(\\s*\"((?:DD|OTEL)_[A-Z0-9_]+)");
const std::regex kEnvGuardRe(
    "\\bEnvGuard\\b[\\w\\s:<>&,*]*[\\(\\{]\\s*\"((?:DD|OTEL)_[A-Z0-9_]+)\"");
const std::regex kStringEnvRe("\"((?:DD|OTEL)_[A-Z0-9_]+)\"");
const std::regex kDefaultResolvedInCodeRe(
    "^ENV_DEFAULT_RESOLVED_IN_CODE\\s*\\((.+)\\)$");
const std::regex kNumericRe("-?(?:[0-9]+(?:\\.[0-9]+)?|\\.[0-9]+)");

struct EnvVarDefinition {
  std::string name;
  std::string type_token;
  std::string default_token;
};

struct Context {
  fs::path repo_root;
  fs::path registry_header;
  fs::path supported_config_path;
};

std::string trim(std::string input) {
  const auto not_space = [](unsigned char c) { return !std::isspace(c); };
  auto begin = std::find_if(input.begin(), input.end(), not_space);
  auto end = std::find_if(input.rbegin(), input.rend(), not_space).base();
  if (begin >= end) {
    return "";
  }
  return std::string(begin, end);
}

void replace_all(std::string &text, const std::string &from,
                 const std::string &to) {
  if (from.empty()) {
    return;
  }
  std::size_t start = 0;
  while ((start = text.find(from, start)) != std::string::npos) {
    text.replace(start, from.size(), to);
    start += to.size();
  }
}

std::string path_to_posix(const fs::path &path) { return path.generic_string(); }

bool is_quoted_string_literal(const std::string &text) {
  return text.size() >= 2 && text.front() == '"' && text.back() == '"';
}

std::string relative_posix_path(const Context &context, const fs::path &path) {
  return path_to_posix(fs::relative(path, context.repo_root));
}

std::string read_text(const fs::path &path) {
  std::ifstream file(path, std::ios::binary);
  if (!file) {
    throw std::runtime_error("Unable to read file: " + path_to_posix(path));
  }
  return std::string(std::istreambuf_iterator<char>(file),
                     std::istreambuf_iterator<char>());
}

std::vector<std::string> find_regex_matches(const std::string &content,
                                            const std::regex &pattern) {
  std::vector<std::string> matches;
  for (auto it = std::sregex_iterator(content.begin(), content.end(), pattern);
       it != std::sregex_iterator(); ++it) {
    matches.push_back((*it)[1].str());
  }
  return matches;
}

std::optional<std::array<std::string, 4>> parse_macro_arguments(
    const std::string &source, std::size_t open_paren, std::size_t &close_paren) {
  std::array<std::string, 4> arguments;
  std::size_t argument_index = 0;
  std::size_t argument_start = open_paren + 1;
  int paren_depth = 0;
  int brace_depth = 0;
  int bracket_depth = 0;
  bool in_single = false;
  bool in_double = false;
  bool in_line_comment = false;
  bool in_block_comment = false;
  bool escape = false;

  for (std::size_t index = open_paren; index < source.size(); ++index) {
    const char ch = source[index];
    const char next = (index + 1 < source.size()) ? source[index + 1] : '\0';

    if (in_line_comment) {
      if (ch == '\n') in_line_comment = false;
      continue;
    }
    if (in_block_comment) {
      if (ch == '*' && next == '/') in_block_comment = false;
      continue;
    }
    if (in_single) {
      if (escape) {
        escape = false;
      } else if (ch == '\\') {
        escape = true;
      } else if (ch == '\'') {
        in_single = false;
      }
      continue;
    }
    if (in_double) {
      if (escape) {
        escape = false;
      } else if (ch == '\\') {
        escape = true;
      } else if (ch == '"') {
        in_double = false;
      }
      continue;
    }

    if (ch == '/' && next == '/') {
      in_line_comment = true;
      continue;
    }
    if (ch == '/' && next == '*') {
      in_block_comment = true;
      continue;
    }
    if (ch == '\'') {
      in_single = true;
      continue;
    }
    if (ch == '"') {
      in_double = true;
      continue;
    }

    if (ch == '(') {
      ++paren_depth;
      continue;
    }
    if (ch == ')') {
      --paren_depth;
      if (paren_depth == 0) {
        close_paren = index;
        if (argument_index < arguments.size()) {
          arguments[argument_index++] =
              trim(source.substr(argument_start, index - argument_start));
        }
        break;
      }
      continue;
    }
    if (ch == '{') {
      ++brace_depth;
      continue;
    }
    if (ch == '}') {
      --brace_depth;
      continue;
    }
    if (ch == '[') {
      ++bracket_depth;
      continue;
    }
    if (ch == ']') {
      --bracket_depth;
      continue;
    }

    if (ch == ',' && paren_depth == 1 && brace_depth == 0 && bracket_depth == 0) {
      if (argument_index >= arguments.size()) return std::nullopt;
      arguments[argument_index++] =
          trim(source.substr(argument_start, index - argument_start));
      argument_start = index + 1;
    }
  }

  if (argument_index != arguments.size()) return std::nullopt;
  return arguments;
}

std::string decode_cpp_single_string_literal(std::string text) {
  std::string body = trim(std::move(text));
  if (!is_quoted_string_literal(body)) {
    throw std::runtime_error("Expected quoted C++ string literal");
  }
  body = body.substr(1, body.size() - 2);
  replace_all(body, "\\\\", "\\");
  replace_all(body, "\\\"", "\"");
  replace_all(body, "\\n", "\n");
  replace_all(body, "\\t", "\t");
  return body;
}

std::string decode_cpp_string_literal_sequence(const std::string &text) {
  const std::string input = trim(text);
  std::string decoded;
  std::size_t index = 0;
  while (index < input.size()) {
    while (index < input.size() &&
           std::isspace(static_cast<unsigned char>(input[index]))) {
      ++index;
    }
    if (index == input.size()) {
      break;
    }
    if (input[index] != '"') {
      throw std::runtime_error(
          "Expected one or more quoted C++ string literals");
    }

    std::size_t end = index + 1;
    bool escaped = false;
    for (; end < input.size(); ++end) {
      const char ch = input[end];
      if (escaped) {
        escaped = false;
        continue;
      }
      if (ch == '\\') {
        escaped = true;
        continue;
      }
      if (ch == '"') {
        break;
      }
    }
    if (end >= input.size()) {
      throw std::runtime_error("Unterminated C++ string literal");
    }

    decoded += decode_cpp_single_string_literal(
        input.substr(index, end - index + 1));
    index = end + 1;
  }
  return decoded;
}

std::vector<EnvVarDefinition> parse_registry_definitions(const Context &context) {
  std::string source = read_text(context.registry_header);
  replace_all(source, "\\\n", " ");

  const std::string token = "MACRO(";
  std::size_t search_index = 0;
  std::vector<EnvVarDefinition> definitions;
  std::set<std::string> seen;

  while (true) {
    const std::size_t match_index = source.find(token, search_index);
    if (match_index == std::string::npos) {
      break;
    }
    const std::size_t open_paren = match_index + std::string("MACRO").size();
    std::size_t close_paren = open_paren;
    const auto arguments = parse_macro_arguments(source, open_paren, close_paren);
    search_index = close_paren + 1;
    if (!arguments) {
      continue;
    }

    const std::string name = trim((*arguments)[1]);
    const std::string type_token = trim((*arguments)[2]);
    const std::string default_token = trim((*arguments)[3]);

    if (!std::regex_match(name, kEnvNameRe)) {
      continue;
    }
    if (seen.count(name)) {
      throw std::runtime_error("Duplicate environment variable entry: " + name);
    }
    seen.insert(name);
    definitions.push_back(EnvVarDefinition{name, type_token, default_token});
  }

  if (definitions.empty()) {
    throw std::runtime_error("No environment variable definitions found in " +
                             path_to_posix(context.registry_header));
  }
  return definitions;
}

std::string normalize_default_token(const std::string &token,
                                    const Context &context) {
  const std::string value = trim(token);
  std::smatch marker;
  if (std::regex_match(value, marker, kDefaultResolvedInCodeRe)) {
    std::string message = trim(marker[1].str());
    if (message.empty() || message.front() != '"') {
      throw std::runtime_error(
          "ENV_DEFAULT_RESOLVED_IN_CODE(...) must contain a quoted string in " +
          path_to_posix(context.registry_header));
    }
    return decode_cpp_string_literal_sequence(message);
  }

  if (is_quoted_string_literal(value)) {
    return decode_cpp_string_literal_sequence(value);
  }
  if (value == "true" || value == "false") {
    return value;
  }
  if (value == "[]") {
    return value;
  }
  if (std::regex_match(value, kNumericRe)) {
    return value;
  }
  throw std::runtime_error("Unsupported default token '" + value + "' in " +
                           path_to_posix(context.registry_header));
}

std::vector<fs::path> iter_source_files(const Context &context,
                                        const std::vector<std::string> &dirs) {
  std::vector<fs::path> paths;
  for (const auto &directory : dirs) {
    const fs::path root = context.repo_root / directory;
    if (!fs::exists(root)) {
      continue;
    }
    for (auto it = fs::recursive_directory_iterator(root);
         it != fs::recursive_directory_iterator(); ++it) {
      if (!it->is_regular_file()) {
        continue;
      }
      if (kScanSuffixes.count(it->path().extension().string()) == 0) {
        continue;
      }
      paths.push_back(it->path());
    }
  }
  std::sort(paths.begin(), paths.end());
  return paths;
}

using UsedVars = std::map<std::string, std::set<std::string>>;

UsedVars discover_used_env_variables(const Context &context) {
  UsedVars used;
  for (const auto &path : iter_source_files(context, kScanDirs)) {
    const std::string content = read_text(path);
    const std::string relative = relative_posix_path(context, path);

    for (const auto &match : find_regex_matches(content, kEnvEnumRe)) {
      used[match].insert(relative);
    }
    for (const auto &match : find_regex_matches(content, kGetenvRe)) {
      used[match].insert(relative);
    }
    for (const auto &match : find_regex_matches(content, kSetenvRe)) {
      used[match].insert(relative);
    }
    for (const auto &match : find_regex_matches(content, kEnvGuardRe)) {
      used[match].insert(relative);
    }
    if (content.find("EnvGuard") != std::string::npos) {
      for (const auto &match : find_regex_matches(content, kStringEnvRe)) {
        used[match].insert(relative);
      }
    }
  }
  return used;
}

using DynamicCalls = std::map<std::string, std::set<std::string>>;

DynamicCalls discover_disallowed_dynamic_getenv_calls(const Context &context) {
  DynamicCalls calls;
  for (const auto &path : iter_source_files(context, kDynamicScanDirs)) {
    const std::string relative = relative_posix_path(context, path);
    if (kAllowedDynamicGetenvPaths.count(relative) > 0) {
      continue;
    }
    const std::string content = read_text(path);
    for (const auto &argument : find_regex_matches(content, kGetenvCallRe)) {
      const auto trimmed = trim(argument);
      if (is_quoted_string_literal(trimmed)) {
        continue;
      }
      calls[relative].insert(trimmed);
    }
  }
  return calls;
}

struct Verification {
  bool ok;
  std::vector<std::string> errors;
};

struct PreflightResult {
  Verification allowlist;
  Verification dynamic_getenv;
};

Verification verify_used_variables_are_allowlisted(
    const std::set<std::string> &allowlisted, const UsedVars &used) {
  std::vector<std::string> missing;
  for (const auto &[var, _] : used) {
    if (!allowlisted.count(var)) {
      missing.push_back(var);
    }
  }
  if (missing.empty()) {
    return {true, {}};
  }

  std::vector<std::string> lines;
  lines.push_back(
      "Found DD_/OTEL_ environment variables used in code that are not declared in "
      "include/datadog/environment_registry.h:");
  for (const auto &variable : missing) {
    std::string locations;
    bool first = true;
    for (const auto &path : used.at(variable)) {
      if (!first) {
        locations += ", ";
      }
      first = false;
      locations += path;
    }
    lines.push_back("  - " + variable + " (" + locations + ")");
  }
  lines.push_back(
      "Add missing variables to include/datadog/environment_registry.h (which "
      "drives include/datadog/environment.h).");
  return {false, lines};
}

Verification verify_no_disallowed_dynamic_getenv_calls(
    const DynamicCalls &dynamic_calls) {
  if (dynamic_calls.empty()) {
    return {true, {}};
  }

  std::vector<std::string> lines;
  lines.push_back(
      "Found dynamic getenv(...) access in include/src. Dynamic environment "
      "variable access is prohibited for tracer configuration paths.");
  for (const auto &[path, arguments] : dynamic_calls) {
    std::string joined;
    bool first = true;
    for (const auto &arg : arguments) {
      if (!first) {
        joined += ", ";
      }
      first = false;
      joined += arg;
    }
    lines.push_back("  - " + path + " (arguments: " + joined + ")");
  }
  lines.push_back(
      "Use include/datadog/environment_registry.h plus environment::lookup<...>() "
      "instead.");
  return {false, lines};
}

PreflightResult run_preflight(const Context &context,
                              const std::vector<EnvVarDefinition> &definitions) {
  std::set<std::string> allowlisted;
  for (const auto &definition : definitions) {
    allowlisted.insert(definition.name);
  }

  return {
      verify_used_variables_are_allowlisted(allowlisted,
                                            discover_used_env_variables(context)),
      verify_no_disallowed_dynamic_getenv_calls(
          discover_disallowed_dynamic_getenv_calls(context)),
  };
}

void print_verification_errors(const Verification &verification) {
  for (const auto &line : verification.errors) {
    std::cout << line << '\n';
  }
}

json build_supported_configurations(const std::vector<EnvVarDefinition> &definitions,
                                    const Context &context) {
  const std::map<std::string, std::string> type_map = {
      {"STRING", "string"}, {"BOOLEAN", "boolean"}, {"INT", "int"},
      {"DECIMAL", "decimal"}, {"ARRAY", "array"},   {"MAP", "map"}};

  std::vector<EnvVarDefinition> sorted = definitions;
  std::sort(sorted.begin(), sorted.end(),
            [](const EnvVarDefinition &a, const EnvVarDefinition &b) {
              return a.name < b.name;
            });

  json supported = json::object();
  for (const auto &definition : sorted) {
    const auto type_it = type_map.find(definition.type_token);
    if (type_it == type_map.end()) {
      throw std::runtime_error("Unsupported type token '" + definition.type_token +
                               "' for " + definition.name);
    }
    supported[definition.name] = json::array(
        {json{{"implementation", "A"},
              {"type", type_it->second},
              {"default", normalize_default_token(definition.default_token, context)}}});
  }

  if (supported.size() != definitions.size()) {
    throw std::runtime_error(
        "Internal error: every registry definition must map to exactly one "
        "supported configuration entry");
  }
  return supported;
}

json load_existing_deprecations(const Context &context) {
  if (!fs::exists(context.supported_config_path)) {
    return json::object();
  }
  try {
    const auto parsed = json::parse(read_text(context.supported_config_path));
    if (parsed.contains("deprecations") && parsed["deprecations"].is_object()) {
      return parsed["deprecations"];
    }
    return json::object();
  } catch (...) {
    return json::object();
  }
}

void write_supported_configurations_json(const Context &context,
                                         const json &supported_configurations) {
  json output = json::object();
  output["version"] = "2";
  output["supportedConfigurations"] = supported_configurations;
  output["deprecations"] = load_existing_deprecations(context);

  fs::create_directories(context.supported_config_path.parent_path());
  std::ofstream file(context.supported_config_path, std::ios::binary);
  if (!file) {
    throw std::runtime_error("Unable to write " +
                             path_to_posix(context.supported_config_path));
  }
  file << output.dump(2) << '\n';
}

int run_check(const Context &context) {
  const auto definitions = parse_registry_definitions(context);
  const auto preflight = run_preflight(context, definitions);

  if (preflight.allowlist.ok && preflight.dynamic_getenv.ok) {
    std::cout << "Environment variable allowlist check passed.\n";
    return 0;
  }
  print_verification_errors(preflight.allowlist);
  print_verification_errors(preflight.dynamic_getenv);
  return 1;
}

int run_generate(const Context &context) {
  const auto definitions = parse_registry_definitions(context);
  const auto preflight = run_preflight(context, definitions);
  if (!preflight.allowlist.ok || !preflight.dynamic_getenv.ok) {
    print_verification_errors(preflight.allowlist);
    print_verification_errors(preflight.dynamic_getenv);
    return 1;
  }

  const auto supported = build_supported_configurations(definitions, context);
  if (supported.empty()) {
    std::cout << "Error: no supported configurations were generated.\n";
    return 1;
  }

  write_supported_configurations_json(context, supported);
  std::cout << "Wrote "
            << path_to_posix(fs::relative(context.supported_config_path,
                                          context.repo_root))
            << '\n';
  return 0;
}

Context make_context(const char *argv0) {
  fs::path executable = fs::absolute(argv0);
  fs::path script_dir = executable.parent_path();
  fs::path repo_root = script_dir.parent_path();
  return Context{
      repo_root,
      repo_root / "include/datadog/environment_registry.h",
      repo_root / "metadata/supported-configurations.json",
  };
}

}  // namespace

int main(int argc, char **argv) {
  try {
    constexpr const char kUsage[] = "Usage: supported-configurations <check|generate>\n";
    if (argc != 2) {
      std::cerr << kUsage;
      return 1;
    }
    const std::string command = argv[1];
    const Context context = make_context(argv[0]);
    if (command == "check") {
      return run_check(context);
    }
    if (command == "generate") {
      return run_generate(context);
    }
    std::cerr << kUsage;
    return 1;
  } catch (const std::exception &error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
