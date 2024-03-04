#include "tracer.h"

#include <datadog/dict_reader.h>
#include <datadog/dict_writer.h>
#include <datadog/span_config.h>
#include <datadog/tracer.h>

#include <iostream>

namespace dd = datadog::tracing;

extern "C" {

class LuaReader : public dd::DictReader {
  ReaderFunc read_;

 public:
  LuaReader(ReaderFunc func) : read_(func) {}

  dd::Optional<dd::StringView> lookup(dd::StringView key) const override {
    if (auto value = read_(key.data())) {
      return value;
    }

    return dd::nullopt;
  }

  void visit(const std::function<void(dd::StringView key,
                                      dd::StringView value)>& /* visitor */)
      const override{};
};

class LuaWriter : public dd::DictWriter {
  WriterFunc write_;

 public:
  LuaWriter(WriterFunc func) : write_(func) {}
  void set(dd::StringView key, dd::StringView value) override {
    write_(key.data(), value.data());
  }
};

// TRACER
void* tracer_new() {
  dd::TracerConfig config;
  config.defaults.service = "luajit-dmehala";

  const auto validated_config = dd::finalize_config(config);
  return new dd::Tracer{*validated_config};
}

void tracer_free(void* p) {
  dd::Tracer* tracer = (dd::Tracer*)p;
  delete tracer;
}

void* tracer_create_span(void* p, const char* name) {
  dd::Tracer* tracer = (dd::Tracer*)p;

  dd::SpanConfig opt;
  opt.name = name;
  void* buffer = new dd::Span(tracer->create_span(opt));

  return buffer;
}

void* tracer_extract_or_create_span(void* p, ReaderFunc lua_reader) {
  dd::Tracer* tracer = (dd::Tracer*)p;

  LuaReader reader(lua_reader);
  auto maybe_span = tracer->extract_or_create_span(reader);
  if (auto error = maybe_span.if_error()) {
    return nullptr;
  }

  void* buffer = new dd::Span(std::move(*maybe_span));
  return buffer;
}

// SPAN
void span_free(void* p) {
  dd::Span* span = (dd::Span*)p;
  delete span;
}

void span_set_tag(void* p, const char* const key, const char* value) {
  dd::Span* span = (dd::Span*)p;
  span->set_tag(key, value);
}

void span_inject(void* p, WriterFunc lua_writer) {
  dd::Span* span = (dd::Span*)p;

  LuaWriter writer(lua_writer);
  span->inject(writer);
}

void* span_create_child(void* p, const char* name) {
  dd::Span* span = (dd::Span*)p;

  dd::SpanConfig config;
  config.name = name;

  void* buffer = new dd::Span(span->create_child(config));

  return buffer;
}

void span_finish(void* p) {
  dd::Span* span = (dd::Span*)p;
  span->set_end_time(std::chrono::steady_clock::now());
}
}
