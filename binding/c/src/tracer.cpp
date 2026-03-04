#include "datadog/c/tracer.h"

#include <datadog/hex.h>
#include <datadog/trace_segment.h>
#include <datadog/tracer.h>

#include <cstddef>
#include <cstring>
#include <string>

namespace dd = datadog::tracing;

namespace {

class ContextReader : public dd::DictReader {
    ddog_trace_context_read_callback read_;

  public:
    explicit ContextReader(ddog_trace_context_read_callback read_callback)
        : read_(read_callback) {}

    dd::Optional<dd::StringView> lookup(dd::StringView key) const override {
        std::string key_str(key);
        if (auto value = read_(key_str.c_str())) {
            return value;
        }
        return dd::nullopt;
    }

    void
    visit(const std::function<void(dd::StringView key, dd::StringView value)>
              & /* visitor */) const override {}
};

class ContextWriter : public dd::DictWriter {
    ddog_trace_context_write_callback write_;

  public:
    explicit ContextWriter(ddog_trace_context_write_callback func)
        : write_(func) {}

    void set(dd::StringView key, dd::StringView value) override {
        std::string key_str(key);
        std::string value_str(value);
        write_(key_str.c_str(), value_str.c_str());
    }
};

} // namespace

extern "C" {

ddog_trace_conf_t *ddog_trace_tracer_conf_new(void) {
    try {
        return reinterpret_cast<ddog_trace_conf_t *>(new dd::TracerConfig);
    } catch (...) {
        return nullptr;
    }
}

void ddog_trace_tracer_conf_free(ddog_trace_conf_t *handle) {
    if (!handle)
        return;
    delete reinterpret_cast<dd::TracerConfig *>(handle);
}

void ddog_trace_tracer_conf_set(ddog_trace_conf_t *handle,
                                ddog_trace_tracer_option option,
                                const char *value) {
    if (!handle || !value)
        return;

    auto *cfg = reinterpret_cast<dd::TracerConfig *>(handle);

    switch (option) {
    case DDOG_TRACE_OPT_SERVICE_NAME:
        cfg->service = value;
        break;
    case DDOG_TRACE_OPT_ENV:
        cfg->environment = value;
        break;
    case DDOG_TRACE_OPT_VERSION:
        cfg->version = value;
        break;
    case DDOG_TRACE_OPT_AGENT_URL:
        cfg->agent.url = value;
        break;
    case DDOG_TRACE_OPT_INTEGRATION_NAME:
        cfg->integration_name = value;
        break;
    case DDOG_TRACE_OPT_INTEGRATION_VERSION:
        cfg->integration_version = value;
        break;
    }
}

ddog_trace_tracer_t *ddog_trace_tracer_new(ddog_trace_conf_t *conf_handle) {
    if (!conf_handle)
        return nullptr;

    auto *config = reinterpret_cast<dd::TracerConfig *>(conf_handle);
    const auto validated_config = dd::finalize_config(*config);
    if (!validated_config) {
        return nullptr;
    }

    try {
        return reinterpret_cast<ddog_trace_tracer_t *>(
            new dd::Tracer{*validated_config});
    } catch (...) {
        return nullptr;
    }
}

void ddog_trace_tracer_free(ddog_trace_tracer_t *tracer_handle) {
    if (!tracer_handle)
        return;
    delete reinterpret_cast<dd::Tracer *>(tracer_handle);
}

ddog_trace_span_t *
ddog_trace_tracer_create_span(ddog_trace_tracer_t *tracer_handle,
                              const char *name) {
    if (!tracer_handle || !name)
        return nullptr;

    auto *tracer = reinterpret_cast<dd::Tracer *>(tracer_handle);
    dd::SpanConfig opt;
    opt.name = name;

    try {
        return reinterpret_cast<ddog_trace_span_t *>(
            new dd::Span(tracer->create_span(opt)));
    } catch (...) {
        return nullptr;
    }
}

ddog_trace_span_t *ddog_trace_tracer_extract_or_create_span(
    ddog_trace_tracer_t *tracer_handle,
    ddog_trace_context_read_callback on_context_read, const char *name,
    const char *resource) {
    if (!tracer_handle || !on_context_read || !name)
        return nullptr;

    auto *tracer = reinterpret_cast<dd::Tracer *>(tracer_handle);
    dd::SpanConfig span_config;
    span_config.name = name;
    if (resource) {
        span_config.resource = resource;
    }

    ContextReader reader(on_context_read);
    try {
        return reinterpret_cast<ddog_trace_span_t *>(
            new dd::Span(tracer->extract_or_create_span(reader, span_config)));
    } catch (...) {
        return nullptr;
    }
}

void ddog_trace_span_free(ddog_trace_span_t *span_handle) {
    if (!span_handle)
        return;
    delete reinterpret_cast<dd::Span *>(span_handle);
}

void ddog_trace_span_set_tag(ddog_trace_span_t *span_handle, const char *key,
                             const char *value) {
    if (!span_handle || !key || !value)
        return;
    reinterpret_cast<dd::Span *>(span_handle)->set_tag(key, value);
}

void ddog_trace_span_set_error(ddog_trace_span_t *span_handle,
                               int error_value) {
    if (!span_handle)
        return;
    reinterpret_cast<dd::Span *>(span_handle)->set_error(error_value != 0);
}

void ddog_trace_span_set_error_message(ddog_trace_span_t *span_handle,
                                       const char *error_message) {
    if (!span_handle || !error_message)
        return;
    reinterpret_cast<dd::Span *>(span_handle)->set_error_message(error_message);
}

void ddog_trace_span_inject(
    ddog_trace_span_t *span_handle,
    ddog_trace_context_write_callback on_context_write) {
    if (!span_handle || !on_context_write)
        return;

    auto *span = reinterpret_cast<dd::Span *>(span_handle);
    ContextWriter writer(on_context_write);
    span->inject(writer);
}

ddog_trace_span_t *ddog_trace_span_create_child(ddog_trace_span_t *span_handle,
                                                const char *name) {
    if (!span_handle || !name)
        return nullptr;

    auto *span = reinterpret_cast<dd::Span *>(span_handle);
    dd::SpanConfig config;
    config.name = name;

    try {
        return reinterpret_cast<ddog_trace_span_t *>(
            new dd::Span(span->create_child(config)));
    } catch (...) {
        return nullptr;
    }
}

void ddog_trace_span_finish(ddog_trace_span_t *span_handle) {
    if (!span_handle)
        return;
    reinterpret_cast<dd::Span *>(span_handle)
        ->set_end_time(std::chrono::steady_clock::now());
}

int ddog_trace_span_get_trace_id(ddog_trace_span_t *span_handle, char *buffer,
                                 size_t buffer_size) {
    if (!span_handle || !buffer || buffer_size == 0)
        return -1;

    auto *span = reinterpret_cast<dd::Span *>(span_handle);
    std::string hex = span->trace_id().hex_padded();

    if (hex.size() >= buffer_size)
        return -1;

    std::strncpy(buffer, hex.c_str(), buffer_size);
    return static_cast<int>(hex.size());
}

int ddog_trace_span_get_span_id(ddog_trace_span_t *span_handle, char *buffer,
                                size_t buffer_size) {
    if (!span_handle || !buffer || buffer_size == 0)
        return -1;

    auto *span = reinterpret_cast<dd::Span *>(span_handle);
    std::string hex = dd::hex_padded(span->id());

    if (hex.size() >= buffer_size)
        return -1;

    std::strncpy(buffer, hex.c_str(), buffer_size);
    return static_cast<int>(hex.size());
}

void ddog_trace_span_set_resource(ddog_trace_span_t *span_handle,
                                  const char *resource) {
    if (!span_handle || !resource)
        return;
    reinterpret_cast<dd::Span *>(span_handle)->set_resource_name(resource);
}

void ddog_trace_span_set_service(ddog_trace_span_t *span_handle,
                                 const char *service) {
    if (!span_handle || !service)
        return;
    reinterpret_cast<dd::Span *>(span_handle)->set_service_name(service);
}

void ddog_trace_span_set_tags(ddog_trace_span_t *span_handle, const char **keys,
                              const char **values, int count) {
    if (!span_handle || !keys || !values || count <= 0)
        return;

    auto *span = reinterpret_cast<dd::Span *>(span_handle);
    for (int i = 0; i < count; ++i) {
        if (keys[i] && values[i]) {
            span->set_tag(keys[i], values[i]);
        }
    }
}

int ddog_trace_span_get_sampling_priority(ddog_trace_span_t *span_handle,
                                          int *priority) {
    if (!span_handle || !priority)
        return -1;

    auto *span = reinterpret_cast<dd::Span *>(span_handle);
    auto decision = span->trace_segment().sampling_decision();
    if (decision) {
        *priority = decision->priority;
        return 1;
    }
    return 0;
}

void ddog_trace_span_set_sampling_priority(ddog_trace_span_t *span_handle,
                                           int priority) {
    if (!span_handle)
        return;
    reinterpret_cast<dd::Span *>(span_handle)
        ->trace_segment()
        .override_sampling_priority(priority);
}

ddog_trace_span_t *
ddog_trace_span_create_child_with_options(ddog_trace_span_t *span_handle,
                                          const char *name, const char *service,
                                          const char *resource) {
    if (!span_handle || !name)
        return nullptr;

    auto *span = reinterpret_cast<dd::Span *>(span_handle);
    dd::SpanConfig config;
    config.name = name;
    if (service) {
        config.service = service;
    }
    if (resource) {
        config.resource = resource;
    }
    try {
        return reinterpret_cast<ddog_trace_span_t *>(
            new dd::Span(span->create_child(config)));
    } catch (...) {
        return nullptr;
    }
}

} // extern "C"
