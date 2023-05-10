#include "httplib.h"

#include <datadog/tracer.h>
#include <datadog/tracer_config.h>

#include <iostream>
#include <chrono> // TODO: no
#include <thread> // TODO: no

int main() {
    datadog::tracing::TracerConfig config;
    config.defaults.service = "dd-trace-cpp-http-server-example";

    auto finalized_config = datadog::tracing::finalize_config(config);
    if (datadog::tracing::Error *error = finalized_config.if_error()) {
        std::cerr << "Error: Datadog is misconfigured. " << *error << '\n';
        return 1;
    }

    datadog::tracing::Tracer tracer{*finalized_config};
    std::this_thread::sleep_for(std::chrono::seconds(10));
}
