#include <datadog/span.h>
#include <datadog/span_config.h>
#include <datadog/tracer.h>
#include <datadog/tracer_config.h>

#include "mocks/loggers.h"
#include "test.h"

using namespace datadog::tracing;

TEST_CASE("smoke") {
  TracerConfig config;
  config.set_service_name("testsvc");
  config.set_logger(std::make_shared<NullLogger>());

  auto maybe_config = config.finalize();
  REQUIRE(maybe_config);

  Tracer tracer{*maybe_config};
  SpanConfig span_config;
  span_config.name = "do.thing";
  Span root = tracer.create_span(span_config);

  span_config.name = "another.thing";
  Span child = root.create_child(span_config);
}
