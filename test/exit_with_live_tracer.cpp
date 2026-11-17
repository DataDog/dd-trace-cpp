// Executable that calls exit() while a Tracer, held by a static wrapper, is
// still alive.

#include <datadog/tracer.h>

#include "mocks/event_schedulers.h"
#include "mocks/http_clients.h"

namespace dd = datadog::tracing;

namespace {

// Created empty before main(), so before the library statics.
std::optional<dd::Tracer> tracer;

}  // namespace

int main() {
  const std::shared_ptr<MockHTTPClient> http_client =
      std::make_shared<MockHTTPClient>();
  // Valid agent reply to avoid errors in log
  http_client->response_status = 200;
  http_client->response_body << "{}";

  const std::shared_ptr<MockEventScheduler> event_scheduler =
      std::make_shared<MockEventScheduler>();

  dd::TracerConfig config;
  config.agent.http_client = http_client;
  config.agent.event_scheduler = event_scheduler;
  config.event_scheduler = event_scheduler;
  const dd::Expected<dd::FinalizedTracerConfig> finalized_config =
      dd::finalize_config(config);
  if (!finalized_config) {
    return EXIT_FAILURE;
  }
  tracer.emplace(*finalized_config);

  // Because the mock scheduler does nothing, the trace chunk is only flushed
  // when the Tracer is destroyed. This trace chunk destruction uses both the
  // OTel process context state and the telemetry singleton.
  tracer->create_span();

  std::exit(EXIT_SUCCESS);
}
