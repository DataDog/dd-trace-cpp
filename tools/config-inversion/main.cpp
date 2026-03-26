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
nlohmann::json to_json_default(const T& value) {
  std::ostringstream oss;
  oss << value;
  return oss.str();
}

template <>
nlohmann::json to_json_default(std::nullptr_t const&) {
  return nullptr;
}

nlohmann::json build_configuration() {
  nlohmann::json j;
  j["version"] = "2";

  auto supported_configurations = nlohmann::json::object();

#define QUOTED_IMPL(ARG) #ARG
#define QUOTED(ARG) QUOTED_IMPL(ARG)

#define ENV_DEFAULT_RESOLVED_IN_CODE(X) ""

#define X(NAME, TYPE, DEFAULT_VALUE)                                       \
  do {                                                                     \
    auto obj = nlohmann::json::object();                                   \
    obj["default"] = to_json_default(DEFAULT_VALUE);                       \
    obj["implementation"] = "A";                                           \
    obj["type"] = QUOTED(TYPE);                                            \
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

  const auto j = build_configuration();
  std::ofstream file(output_file, std::ios::binary);
  if (!file) {
    std::cerr << "Unable to write configuration file";
    return 1;
  }

  file << j.dump(2);
  return 0;
}
