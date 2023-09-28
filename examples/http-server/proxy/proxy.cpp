#include <datadog/tracer.h>

#include <iostream>
#include <optional>
#include <string_view>

#include "datadog/dict_reader.h"
#include "datadog/dict_writer.h"
#include "datadog/span.h"
#include "datadog/span_config.h"
#include "datadog/trace_segment.h"
#include "httplib-helper.hpp"
#include "httplib.h"

namespace dd = datadog::tracing;

int main() {
  // Set up the Datadog tracer.  See `src/datadog/tracer_config.h`.
  dd::TracerConfig config;
  config.defaults.service = "dd-trace-cpp-http-server-example-proxy";
  config.defaults.service_type = "proxy";

  // `finalize_config` validates `config` and applies any settings from
  // environment variables, such as `DD_AGENT_HOST`.
  // If the resulting configuration is valid, it will return a
  // `FinalizedTracerConfig` that can then be used to initialize a `Tracer`.
  // If the resulting configuration is invalid, then it will return an
  // `Error` that can be printed, but then no `Tracer` can be created.
  dd::Expected<dd::FinalizedTracerConfig> finalized_config =
      dd::finalize_config(config);
  if (dd::Error* error = finalized_config.if_error()) {
    std::cerr << "Error: Datadog is misconfigured. " << *error << '\n';
    return 1;
  }

  dd::Tracer tracer{*finalized_config};

  httplib::Client upstream_client("server", 80);

  // Configure the HTTP server.
  auto forward_handler = [&tracer, &upstream_client](
                             const httplib::Request& req,
                             httplib::Response& res) {
    auto span = tracer.create_span();
    span.set_name("forward.request");
    span.set_resource_name(req.method + " " + req.path);
    span.set_tag("network.origin.ip", req.remote_addr);
    span.set_tag("network.origin.port", std::to_string(req.remote_port));
    span.set_tag("http.url_details.path", req.target);
    span.set_tag("http.route", req.path);
    span.set_tag("http.method", req.method);

    httplib::Error er;
    httplib::Request forward_request(req);

    // TODO: suggest to use lambda instead of functors.
    // span.inject([&headers = forward_request.headers](std::string_view key,
    //                                                  std::string_view value)
    //                                                  {
    //   headers.emplace(key, value);
    // });

    helper::HeaderWriter writer(forward_request.headers);
    span.inject(writer);

    forward_request.path = req.target;
    upstream_client.send(forward_request, res, er);
    if (er != httplib::Error::Success) {
      res.status = 500;
      span.set_error_message(httplib::to_string(er));
    } else {
      helper::HeaderReader reader(res.headers);
      span.trace_segment().extract(reader);
    }

    span.set_tag("http.status_code", std::to_string(res.status));
  };

  httplib::Server server;
  server.Get(".*", forward_handler);
  server.Post(".*", forward_handler);
  server.Put(".*", forward_handler);
  server.Options(".*", forward_handler);
  server.Patch(".*", forward_handler);
  server.Delete(".*", forward_handler);

  server.listen("0.0.0.0", 80);

  return 0;
}
