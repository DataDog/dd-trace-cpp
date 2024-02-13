#include <datadog/tracer.h>

#include <chrono>
#include <iostream>
#include <thread>

namespace dd = datadog::tracing;

using namespace std::chrono_literals;

static void log(const std::string& msg) {
  std::time_t t = std::time(0);  // get time now
  auto now = std::localtime(&t);

  std::cerr << "[" << std::put_time(now, "%c") << "] " << msg << "\n";
}

int main() {
  dd::TracerConfig config;
  config.defaults.service = "dd-trace-cpp-example";
  config.defaults.environment = "gameday";
  config.defaults.name = "wait";

  auto validated = dd::finalize_config(config);
  if (auto* error = validated.if_error()) {
    std::cerr << "Invalid config: " << *error << '\n';
    return 1;
  }

  dd::Tracer tracer{*validated};

  while (true) {
    auto span = tracer.create_span();
    log("generate span");
    std::this_thread::sleep_for(1s);
  }

  return 0;
}
