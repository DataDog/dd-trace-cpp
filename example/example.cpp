#include <datadog/tracer_config.h>

#include <iostream>

int main() {
    namespace dd = datadog::tracing;
    dd::TracerConfig config;
    config.spans.service = "foosvc";
    std::cout << "config.spans.service: " << config.spans.service << '\n';
    
    std::get<dd::DatadogAgentConfig>(config.collector).http_client = nullptr;
}
