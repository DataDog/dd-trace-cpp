#include "httplib.h"

#include <datadog/tracer.h>
#include <datadog/tracer_config.h>

#include <csignal>
#include <cstdlib>
#include <iostream>

// Alias some namespaces for brevity.
namespace dd = datadog::tracing;
namespace http = httplib;

// `hard_stop` is installed as a signal handler for `SIGTERM`.
// For some reason, the default handler was not being called.
void hard_stop(int /*signal*/) {
    std::exit(0);
}

int main() {
    // Set up the Datadog tracer.
    dd::TracerConfig config;
    config.defaults.service = "dd-trace-cpp-http-server-example";

    dd::Expected<dd::FinalizedTracerConfig> finalized_config = dd::finalize_config(config);
    if (dd::Error *error = finalized_config.if_error()) {
        std::cerr << "Error: Datadog is misconfigured. " << *error << '\n';
        return 1;
    }

    dd::Tracer tracer{*finalized_config};

    // Configure the HTTP server.
    http::Server server;

    server.Get("/healthcheck", [&](const http::Request& request, http::Response& response) {
        dd::Span span = tracer.create_span();
        span.set_name("handle.request");
        span.set_resource_name(request.method + " " + request.path);

        response.set_content("still here\n", "text/plain");
    });

    // Run the HTTP server.
    std::signal(SIGTERM, hard_stop);
    server.listen("0.0.0.0", 8000);
}
