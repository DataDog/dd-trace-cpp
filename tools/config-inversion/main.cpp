#include <algorithm>
#include <cxxopts.hpp>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <nlohmann/json.hpp>
#include <sstream>

#include "datadog/environment.h"

namespace fs = std::filesystem;
namespace env = datadog::tracing::environment;

template <typename T>
std::string to_string_any(const T& value) {
  std::ostringstream oss;
  // boolalpha ensures bools are serialized as "true"/"false" instead of "1"/"0".
  oss << std::boolalpha << value;
  return oss.str();
}

// Look up the existing implementation version for a config name from a
// previously generated JSON. Returns "A" if not found.
std::string existing_version(const nlohmann::json& existing,
                             const std::string& name) {
  if (existing.contains("supportedConfigurations")) {
    const auto& sc = existing["supportedConfigurations"];
    if (sc.contains(name) && sc[name].is_array() && !sc[name].empty()) {
      const auto& first = sc[name][0];
      if (first.contains("implementation")) {
        return first["implementation"].get<std::string>();
      }
    }
  }
  return "A";
}

nlohmann::json build_configuration(const nlohmann::json& existing) {
  nlohmann::json j;
  j["version"] = "2";

  auto supported_configurations = nlohmann::json::object();

#define QUOTED_IMPL(ARG) #ARG
#define QUOTED(ARG) QUOTED_IMPL(ARG)

#define ENV_DEFAULT_RESOLVED_IN_CODE(X) ""

#define X(NAME, TYPE, DEFAULT_VALUE)                                       \
  do {                                                                     \
    auto obj = nlohmann::json::object();                                   \
    obj["default"] = to_string_any(DEFAULT_VALUE);                         \
    obj["implementation"] = existing_version(existing, QUOTED(NAME));      \
    {                                                                      \
      std::string type_str = QUOTED(TYPE);                                 \
      std::transform(type_str.begin(), type_str.end(), type_str.begin(),   \
                     [](unsigned char c) { return std::tolower(c); });     \
      obj["type"] = type_str;                                              \
    }                                                                      \
    supported_configurations[QUOTED(NAME)] = nlohmann::json::array({obj}); \
  } while (0);

  DD_LIST_ENVIRONMENT_VARIABLES(X)
#undef X
#undef ENV_DEFAULT_RESOLVED_IN_CODE

  j["supportedConfigurations"] = supported_configurations;

  return j;
}

int main(int argc, char** argv) {
  cxxopts::Options options("generate-configuration",
                           "A tool to generate a JSON file with Datadog "
                           "configuration supported by the C++ tracer.");

  options.add_options()("o,output-file",
                        "Location where the JSON file will be written",
                        cxxopts::value<std::string>())("h,help", "Print usage");

  auto result = options.parse(argc, argv);

  if (result.count("output-file") == 0 || result.count("help")) {
    std::cout << options.help() << std::endl;
    return 0;
  }

  const fs::path output_file = result["output-file"].as<std::string>();

  // Read existing file to preserve implementation versions.
  nlohmann::json existing;
  {
    std::ifstream in(output_file);
    if (in) {
      existing = nlohmann::json::parse(in, nullptr, false);
      if (existing.is_discarded()) existing = nlohmann::json::object();
    }
  }

  const auto j = build_configuration(existing);
  std::ofstream file(output_file, std::ios::binary);
  if (!file) {
    std::cerr << "Unable to write configuration file";
    return 1;
  }

  file << j.dump(2);
  return 0;
}
