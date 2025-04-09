// tracer_example.cc
#include <datadog/span_config.h>
#include <datadog/tracer.h>
#include <datadog/tracer_config.h>
#include <sys/wait.h>
#include <unistd.h>

#include <chrono>
#include <iostream>

#include "httpclient.h"

namespace dd = datadog::tracing;

// Function to calculate fibonacci number recursively (CPU intensive)
uint64_t fibonacci(int n) {
  if (n <= 1) return n;
  return fibonacci(n - 1) + fibonacci(n - 2);
}

void forking_process(dd::Tracer &tracer, int fib_n) {
  // Create one span per second for 200 seconds
  for (int i = 0; i < 200; i++) {
    auto span = tracer.create_span();
    span.set_resource_name("fibonacci-calculation");
    span.set_tag("iteration", std::to_string(i));
    span.set_tag("fibonacci_n", std::to_string(fib_n));

    // This should take approximately 1 second
    fibonacci(fib_n);
  }
}

void parent_monitor(dd::Tracer &tracer, pid_t child_pid) {
  std::cout << "[Parent] Waiting for child (PID " << child_pid
            << ") to finish..." << std::endl;

  auto span = tracer.create_span();
  span.set_resource_name("parent_monitor");
  int status;
  pid_t result = waitpid(child_pid, &status, 0);

  if (result == -1) {
    perror("waitpid");
  } else if (WIFEXITED(status)) {
    auto span = tracer.create_span();
    span.set_resource_name("child over");
    std::cout << "[Parent] Child exited with status " << WEXITSTATUS(status)
              << std::endl;
  } else {
    auto span = tracer.create_span();
    span.set_resource_name("child abnormal");
    std::cout << "[Parent] Child did not exit normally." << std::endl;
  }
}

int main() {
  dd::TracerConfig config;
  config.agent.http_client =
      std::make_shared<HttplibClient>("http://localhost:3000");

  const auto validated_config = dd::finalize_config(config);
  if (!validated_config) {
    std::cerr << validated_config.error() << '\n';
    return 1;
  }

  dd::Tracer tracer{*validated_config};

  // First, calibrate the fibonacci number that takes ~1 second
  int fib_n = 40;  // Starting point - adjust based on your CPU speed
  auto calibration_span = tracer.create_span();
  calibration_span.set_resource_name("fibonacci-calibration");

  auto start_time = std::chrono::steady_clock::now();
  fibonacci(fib_n);
  auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(
                      std::chrono::steady_clock::now() - start_time)
                      .count();

  std::cout << "Calibration: n=" << fib_n << " took " << duration << "ms"
            << std::endl;

  pid_t pid = fork();
  if (pid < 0) {
    std::cerr << "[Error] Failed to fork process." << std::endl;
    return 1;
  } else if (pid == 0) {
    // Child process
    forking_process(tracer, fib_n);
  } else {
    // Parent process
    parent_monitor(tracer, pid);
  }

  return 0;
}
