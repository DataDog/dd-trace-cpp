#include <datadog/span_config.h>
#include <datadog/tracer.h>

namespace dd = datadog::tracing;

extern "C" {
void* make_tracer() {
  dd::TracerConfig config;
  config.defaults.service = "luajit-dmehala";

  const auto validated_config = dd::finalize_config(config);
  return new dd::Tracer{*validated_config};
}

void tracer_gc(void* p) {
  dd::Tracer* tracer = (dd::Tracer*)p;
  delete tracer;
}

void* create_span(void* p) {
  dd::Tracer* tracer = (dd::Tracer*)p;

  dd::SpanConfig opt;
  opt.name = "root_span";
  void* buffer = new dd::Span(tracer->create_span(opt));

  return buffer;
}

void span_gc(void* p) {
  dd::Span* span = (dd::Span*)p;
  delete span;
}

void set_span(void* p, const char* const key, const char* const value) {
  dd::Span* span = (dd::Span*)p;
  span->set_tag(key, value);
}

int return_one_two_four() { return 124; }
}
