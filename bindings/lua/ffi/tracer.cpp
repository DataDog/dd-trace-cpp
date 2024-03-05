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

// TRACER CONFIG
void* tracer_config_new() { return new dd::TracerConfig; }
void tracer_config_free(void* p) {
  dd::TracerConfig* cfg = (dd::TracerConfig*)p;
  cfg->defaults.tags.emplace("bindings.language", "luajit");
  // cfg->defaults.tags.emplace("bindings.version", lua_version);
  // cfg->integration_name = "lua";
  // cfg->integration_version = lua_version;
  delete cfg;
}

void tracer_config_set(void* p, int opt, void* value) {
  dd::TracerConfig* cfg = (dd::TracerConfig*)p;
  if (opt == 0) {
    const char* service = (const char*)value;
    cfg->defaults.service = service;
  } else if (opt == 1) {
    const char* env = (const char*)value;
    cfg->defaults.environment = env;
  } else if (opt == 2) {
    const char* version = (const char*)value;
    cfg->defaults.version = version;
  } else if (opt == 3) {
    const char* agent_url = (const char*)value;
    cfg->agent.url = agent_url;
  }
}

// TRACER
void* tracer_new(void* p) {
  dd::TracerConfig* config = (dd::TracerConfig*)p;

  const auto validated_config = dd::finalize_config(*config);
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

void* tracer_extract_or_create_span(void* p, ReaderFunc lua_reader,
                                    const char* name, const char* resource) {
  dd::Tracer* tracer = (dd::Tracer*)p;

  dd::SpanConfig span_config;
  span_config.name = name;
  span_config.resource = resource;

  LuaReader reader(lua_reader);
  auto maybe_span = tracer->extract_or_create_span(reader, span_config);
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

void span_set_error(void* p, bool b) {
  dd::Span* span = (dd::Span*)p;
  span->set_error(b);
}

void span_set_error_message(void* p, const char* msg) {
  dd::Span* span = (dd::Span*)p;
  span->set_error_message(msg);
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
