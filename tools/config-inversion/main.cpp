#include <cxxopts.hpp>
#include <cctype>
#include <fstream>
#include <iostream>
#include <nlohmann/json.hpp>
#include <sstream>
#include <string>
#include <type_traits>

#include "datadog/environment.h"

std::string lowercase(std::string value) {
  for (char& ch : value) {
    ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
  }
  return value;
}

template <typename T>
nlohmann::json json_default_value(const std::string& type, const char* raw_token,
                                  const T& value) {
  using Value = std::decay_t<T>;

  if constexpr (std::is_same_v<Value, std::nullptr_t>) {
    return nullptr;
  }

  if (type == "decimal") {
    return raw_token;
  }

  if constexpr (std::is_same_v<Value, bool>) {
    return value ? "true" : "false";
  }

  std::ostringstream oss;
  oss << value;
  return oss.str();
}

nlohmann::json load_existing_supported_configurations() {
  std::ifstream file("supported-configurations.json", std::ios::binary);
  nlohmann::json json;
  file >> json;
  const auto it = json.find("supportedConfigurations");
  return it != json.end() && it->is_object() ? *it : nlohmann::json::object();
}

std::string preserved_implementation(
    const nlohmann::json& supported_configurations, const char* name) {
  const auto it = supported_configurations.find(name);
  if (it == supported_configurations.end() || !it->is_array() || it->empty() ||
      !(*it)[0].is_object()) {
    return "A";
  }

  return (*it)[0].value("implementation", "A");
}

nlohmann::json build_configuration() {
  const auto existing_supported_configurations =
      load_existing_supported_configurations();

  auto supported_configurations = nlohmann::json::object();

#define QUOTED_IMPL(ARG) #ARG
#define QUOTED(ARG) QUOTED_IMPL(ARG)
#define RAW_QUOTED(ARG) #ARG

#define ENV_DEFAULT_RESOLVED_IN_CODE(X) ""

#define X(NAME, TYPE, DEFAULT_VALUE)                                           \
  do {                                                                         \
    const auto type = lowercase(QUOTED(TYPE));                                 \
    supported_configurations[QUOTED(NAME)] = nlohmann::json::array({           \
        {{"default",                                                           \
          json_default_value(type, RAW_QUOTED(DEFAULT_VALUE), DEFAULT_VALUE)}, \
         {"implementation",                                                    \
          preserved_implementation(existing_supported_configurations,          \
                                   QUOTED(NAME))},                             \
         {"type", type}}});                                                    \
  } while (0);

  DD_LIST_ENVIRONMENT_VARIABLES(X)
#undef X
#undef ENV_DEFAULT_RESOLVED_IN_CODE
#undef RAW_QUOTED

  return {{"supportedConfigurations", std::move(supported_configurations)},
          {"version", "2"}};
}

int main(int argc, char** argv) {
  cxxopts::Options options("generate-configuration",
                           "A tool to generate a JSON file with Datadog "
                           "configuration supported by the C++ tracer.");

  options.add_options()("o,output-file",
                        "Location where the JSON file will be written",
                        cxxopts::value<std::string>())("h,help", "Print usage");

  const auto result = options.parse(argc, argv);
  if (result.count("output-file") == 0 || result.count("help")) {
    std::cout << options.help() << std::endl;
    return 0;
  }

  const auto output = build_configuration().dump(2);
  std::ofstream file(result["output-file"].as<std::string>(), std::ios::binary);
  if (!file) {
    std::cerr << "Unable to write configuration file";
    return 1;
  }

  file << output;
  return 0;
}
