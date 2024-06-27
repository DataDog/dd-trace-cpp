#include "datadog/c/tracer.h"

#include <datadog/dict_reader.h>
#include <datadog/dict_writer.h>
#include <datadog/span_config.h>
#include <datadog/tracer.h>

namespace dd = datadog::tracing;

#if defined(__cplusplus)
extern "C" {
#endif

static inline dd::StringView str_view_to_dd_view(str_view s) {
  return dd::StringView(s.buf, s.len);
}

static inline std::string str_view_to_string(str_view s) {
  return std::string(s.buf, s.len);
}

class ContextReader : public dd::DictReader {
  datadog_sdk_context_read_callback read_;
  void* context_;

 public:
  ContextReader(void* context, datadog_sdk_context_read_callback func)
      : read_(func), context_(context) {}

  dd::Optional<dd::StringView> lookup(dd::StringView key) const override {
    str_view value = read_(context_, str_view{
                                         .buf = key.data(),
                                         .len = key.length(),
                                     });
    if (value.buf) {
      return dd::StringView(value.buf, value.len);
    }
    return dd::nullopt;
  }

  void visit(const std::function<void(dd::StringView key,
                                      dd::StringView value)>& /* visitor */)
      const override {};
};

class ContextWriter : public dd::DictWriter {
  datadog_sdk_context_write_callback write_;
  void* context_;

 public:
  ContextWriter(void* context, datadog_sdk_context_write_callback func)
      : write_(func), context_(context) {}
  void set(dd::StringView key, dd::StringView value) override {
    write_(context_, str_view{.buf = key.data(), .len = key.length()},
           str_view{.buf = value.data(), .len = value.length()});
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
    str_view service = *(str_view*)value;
    cfg->service = str_view_to_string(service);
  } else if (option == DATADOG_TRACER_OPT_ENV) {
    str_view env = *(str_view*)value;
    cfg->environment = str_view_to_string(env);
  } else if (option == DATADOG_TRACER_OPT_VERSION) {
    str_view version = *(str_view*)value;
    cfg->version = str_view_to_string(version);
  } else if (option == DATADOG_TRACER_OPT_AGENT_URL) {
    str_view agent_url = *(str_view*)value;
    cfg->agent.url = str_view_to_string(agent_url);
  } else if (option == DATADOG_TRACER_OPT_LIBRARY_LANGUAGE) {
    str_view library_language = *(str_view*)value;
    cfg->library_language = str_view_to_string(library_language);
  } else if (option == DATADOG_TRACER_OPT_LIBRARY_VERSION) {
    str_view s = *(str_view*)value;
    cfg->library_version = str_view_to_string(s);
  } else if (option == DATADOG_TRACER_OPT_LIBRARY_LANGUAGE_VERSION) {
    str_view s = *(str_view*)value;
    cfg->library_language_version = str_view_to_string(s);
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

void datadog_sdk_tracer_flush(datadog_sdk_tracer_t* tracer_handle) {
  auto* tracer = static_cast<dd::Tracer*>(tracer_handle);
  tracer->flush();
}

datadog_sdk_span_t* datadog_sdk_tracer_create_span(
    datadog_sdk_tracer_t* tracer_handle, str_view name) {
  auto* tracer = static_cast<dd::Tracer*>(tracer_handle);

  dd::SpanConfig opt;
  opt.name = str_view_to_string(name);

  void* buffer = new dd::Span(tracer->create_span(opt));
  return buffer;
}

datadog_sdk_span_t* datadog_sdk_tracer_extract_or_create_span(
    datadog_sdk_tracer_t* tracer_handle, void* reader_ctx,
    datadog_sdk_context_read_callback on_context_read, str_view name,
    str_view resource) {
  auto* tracer = static_cast<dd::Tracer*>(tracer_handle);

  dd::SpanConfig span_config;
  span_config.name = str_view_to_string(name);
  span_config.resource = str_view_to_string(resource);

  ContextReader reader(reader_ctx, on_context_read);
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

void datadog_sdk_span_set_tag(datadog_sdk_span_t* span_handle, str_view key,
                              str_view value) {
  auto* span = static_cast<dd::Span*>(span_handle);
  span->set_tag(str_view_to_dd_view(key), str_view_to_dd_view(value));
}

void datadog_sdk_span_set_type(datadog_sdk_span_t* span_handle, str_view type) {
  auto* span = static_cast<dd::Span*>(span_handle);
  span->service_type() = str_view_to_dd_view(type);
}

void datadog_sdk_span_set_error(datadog_sdk_span_t* span_handle,
                                int error_value) {
  auto* span = static_cast<dd::Span*>(span_handle);
  span->set_error(error_value != 0);
}

void datadog_sdk_span_set_error_message(datadog_sdk_span_t* span_handle,
                                        str_view error_message) {
  auto* span = static_cast<dd::Span*>(span_handle);
  span->set_error_message(str_view_to_dd_view(error_message));
}

void datadog_sdk_span_inject(
    datadog_sdk_span_t* span_handle, void* writer_ctx,
    datadog_sdk_context_write_callback on_context_write) {
  auto* span = static_cast<dd::Span*>(span_handle);

  ContextWriter writer(writer_ctx, on_context_write);
  span->inject(writer);
}

void* datadog_sdk_span_create_child(datadog_sdk_span_t* span_handle,
                                    str_view name) {
  auto* span = static_cast<dd::Span*>(span_handle);

  dd::SpanConfig config;
  config.name = str_view_to_string(name);

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
