#include "datadog/c/tracer.h"

#include <datadog/dict_reader.h>
#include <datadog/dict_writer.h>
#include <datadog/span_config.h>
#include <datadog/tracer.h>

namespace dd = datadog::tracing;

#if defined(__cplusplus)
extern "C" {
#endif

class ContextReader : public dd::DictReader {
  datadog_sdk_context_read_callback read_;

 public:
  ContextReader(datadog_sdk_context_read_callback read_callback)
      : read_(read_callback) {}

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

class ContextWriter : public dd::DictWriter {
  datadog_sdk_context_write_callback write_;

 public:
  ContextWriter(datadog_sdk_context_write_callback func) : write_(func) {}

  void set(dd::StringView key, dd::StringView value) override {
    write_(key.data(), value.data());
  }
};

datadog_sdk_conf_t* datadog_sdk_tracer_conf_new() {
  return new dd::TracerConfig;
}

void datadog_sdk_tracer_conf_free(datadog_sdk_conf_t* p) {
  delete static_cast<dd::TracerConfig*>(p);
}

void datadog_sdk_tracer_conf_set(datadog_sdk_conf_t* handle,
                                 enum datadog_sdk_tracer_option option,
                                 void* value) {
  auto cfg = static_cast<dd::TracerConfig*>(handle);
  if (option == DATADOG_TRACER_OPT_SERVICE_NAME) {
    const char* service = (const char*)value;
    cfg->service = service;
  } else if (option == DATADOG_TRACER_OPT_ENV) {
    const char* env = (const char*)value;
    cfg->environment = env;
  } else if (option == DATADOG_TRACER_OPT_VERSION) {
    const char* version = (const char*)value;
    cfg->version = version;
  } else if (option == DATADOG_TRACER_OPT_AGENT_URL) {
    const char* agent_url = (const char*)value;
    cfg->agent.url = agent_url;
  }
}

datadog_sdk_tracer_t* datadog_sdk_tracer_new(datadog_sdk_conf_t* conf_handle) {
  auto* config = static_cast<dd::TracerConfig*>(conf_handle);

  const auto validated_config = dd::finalize_config(*config);
  if (!validated_config) {
    // Find a better way to propagate the error
    return nullptr;
  }

  return new dd::Tracer{*validated_config};
}

void datadog_sdk_tracer_free(datadog_sdk_tracer_t* tracer_handle) {
  delete static_cast<dd::Tracer*>(tracer_handle);
}

datadog_sdk_span_t* datadog_sdk_tracer_create_span(
    datadog_sdk_tracer_t* tracer_handle, const char* name) {
  auto* tracer = static_cast<dd::Tracer*>(tracer_handle);

  dd::SpanConfig opt;
  opt.name = name;

  void* buffer = new dd::Span(tracer->create_span(opt));
  return buffer;
}

datadog_sdk_span_t* datadog_sdk_tracer_extract_or_create_span(
    datadog_sdk_tracer_t* tracer_handle,
    datadog_sdk_context_read_callback on_context_read, const char* name,
    const char* resource) {
  auto* tracer = static_cast<dd::Tracer*>(tracer_handle);

  dd::SpanConfig span_config;
  span_config.name = name;
  span_config.resource = resource;

  ContextReader reader(on_context_read);
  auto maybe_span = tracer->extract_or_create_span(reader, span_config);
  if (auto error = maybe_span.if_error()) {
    return nullptr;
  }

  void* buffer = new dd::Span(std::move(*maybe_span));
  return buffer;
}

void datadog_sdk_span_free(datadog_sdk_span_t* span_handle) {
  delete static_cast<dd::Span*>(span_handle);
}

void datadog_sdk_span_set_tag(datadog_sdk_span_t* span_handle, const char* key,
                              const char* value) {
  auto* span = static_cast<dd::Span*>(span_handle);
  span->set_tag(key, value);
}

void datadog_sdk_span_set_error(datadog_sdk_span_t* span_handle,
                                int error_value) {
  auto* span = static_cast<dd::Span*>(span_handle);
  span->set_error(error_value != 0);
}

void datadog_sdk_span_set_error_message(datadog_sdk_span_t* span_handle,
                                        const char* error_message) {
  auto* span = static_cast<dd::Span*>(span_handle);
  span->set_error_message(error_message);
}

void datadog_sdk_span_inject(
    datadog_sdk_span_t* span_handle,
    datadog_sdk_context_write_callback on_context_write) {
  auto* span = static_cast<dd::Span*>(span_handle);

  ContextWriter writer(on_context_write);
  span->inject(writer);
}

void* datadog_sdk_span_create_child(datadog_sdk_span_t* span_handle,
                                    const char* name) {
  auto* span = static_cast<dd::Span*>(span_handle);

  dd::SpanConfig config;
  config.name = name;

  void* buffer = new dd::Span(span->create_child(config));
  return buffer;
}

void datadog_sdk_span_finish(datadog_sdk_span_t* span_handle) {
  auto* span = static_cast<dd::Span*>(span_handle);
  span->set_end_time(std::chrono::steady_clock::now());
}

#if defined(__cplusplus)
}
#endif
