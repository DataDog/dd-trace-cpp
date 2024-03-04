#ifndef DD_TRACE_C_BINDING
#define DD_TRACE_C_BINDING

extern "C" {

typedef const char* (*ReaderFunc)(const char*);
typedef void (*WriterFunc)(const char*, const char*);

// TracerConfig
// int DD_TRACE_CONFIG_AGENT_HOST = 0;

void* tracer_config_new();
void tracer_config_free(void*);
void tracer_config_set(void*, int, void*);

// Tracer
void* tracer_new(void*);
void tracer_free(void*);
void* tracer_create_span(void*, const char*);
void* tracer_extract_or_create_span(void*, ReaderFunc);

// Span
void span_free(void*);
void span_set_tag(void*, const char*, const char*);
void span_inject(void*, WriterFunc);
void* span_create_child(void*, const char*);
void span_finish(void*);
}

#endif
