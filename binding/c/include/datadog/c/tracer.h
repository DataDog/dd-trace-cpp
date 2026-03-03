#ifndef DDOG_TRACE_C_TRACER_H
#define DDOG_TRACE_C_TRACER_H

#if defined(_WIN32)
#if defined(DDOG_TRACE_C_BUILDING)
#define DDOG_TRACE_C_API __declspec(dllexport)
#else
#define DDOG_TRACE_C_API __declspec(dllimport)
#endif
#elif defined(__GNUC__) || defined(__clang__)
#define DDOG_TRACE_C_API __attribute__((visibility("default")))
#else
#define DDOG_TRACE_C_API
#endif

#if defined(__cplusplus)
extern "C" {
#endif

// Callback used during trace context extraction. The tracer calls this
// function for each propagation header it needs to read (e.g. "x-datadog-*").
//
// @param key  Header name to look up
// @return     Header value, or NULL if the header is not present.
//             The returned pointer must remain valid until
//             ddog_trace_tracer_extract_or_create_span returns.
typedef const char* (*ddog_trace_context_read_callback)(const char* key);

// Callback used during trace context injection. The tracer calls this
// function for each propagation header it needs to write.
//
// @param key    Header name to set
// @param value  Header value to set
typedef void (*ddog_trace_context_write_callback)(const char* key,
                                                  const char* value);

enum ddog_trace_tracer_option {
  DDOG_TRACE_OPT_SERVICE_NAME = 0,
  DDOG_TRACE_OPT_ENV = 1,
  DDOG_TRACE_OPT_VERSION = 2,
  DDOG_TRACE_OPT_AGENT_URL = 3,
  DDOG_TRACE_OPT_INTEGRATION_NAME = 4,
  DDOG_TRACE_OPT_INTEGRATION_VERSION = 5
};

typedef void ddog_trace_conf_t;
typedef void ddog_trace_tracer_t;
typedef void ddog_trace_span_t;

// Creates a tracer configuration instance.
//
// @return Configuration handle, or NULL on allocation failure
DDOG_TRACE_C_API ddog_trace_conf_t* ddog_trace_tracer_conf_new();

// Release a tracer configuration. Safe to call with NULL.
//
// @param handle Configuration handle to release
DDOG_TRACE_C_API void ddog_trace_tracer_conf_free(ddog_trace_conf_t* handle);

// Set or update a configuration field. No-op if handle or value is NULL.
//
// @param handle  Configuration handle
// @param option  Configuration option
// @param value   Configuration value
DDOG_TRACE_C_API void ddog_trace_tracer_conf_set(
    ddog_trace_conf_t* handle, enum ddog_trace_tracer_option option,
    const char* value);

// Creates a tracer instance.
//
// @param conf_handle Configuration handle
//
// @return Tracer handle, or NULL on error (e.g. invalid config)
DDOG_TRACE_C_API ddog_trace_tracer_t* ddog_trace_tracer_new(
    ddog_trace_conf_t* conf_handle);

// Release a tracer instance. Safe to call with NULL.
//
// @param tracer_handle Tracer handle to release
DDOG_TRACE_C_API void ddog_trace_tracer_free(
    ddog_trace_tracer_t* tracer_handle);

// Create a span using a Tracer.
//
// @param tracer_handle Tracer handle
// @param name          Name of the span
//
// @return Span handle, or NULL on error
DDOG_TRACE_C_API ddog_trace_span_t* ddog_trace_tracer_create_span(
    ddog_trace_tracer_t* tracer_handle, const char* name);

// Extract trace context from incoming headers, or create a new root span
// if extraction fails. Never returns an error span; on extraction failure
// a fresh root span is created.
//
// @param tracer_handle    Tracer handle
// @param on_context_read  Callback invoked to read propagation headers
// @param name             Span name
// @param resource         Resource name (may be NULL)
//
// @return Span handle, or NULL if arguments are invalid
DDOG_TRACE_C_API ddog_trace_span_t* ddog_trace_tracer_extract_or_create_span(
    ddog_trace_tracer_t* tracer_handle,
    ddog_trace_context_read_callback on_context_read, const char* name,
    const char* resource);

// Release a span instance. Safe to call with NULL.
//
// @param span_handle Span handle
DDOG_TRACE_C_API void ddog_trace_span_free(ddog_trace_span_t* span_handle);

// Set a tag (key-value pair) on a span. No-op if any argument is NULL.
//
// @param span_handle Span handle
// @param key         Tag key
// @param value       Tag value
DDOG_TRACE_C_API void ddog_trace_span_set_tag(ddog_trace_span_t* span_handle,
                                              const char* key,
                                              const char* value);

// Mark a span as erroneous. No-op if span_handle is NULL.
//
// @param span_handle Span handle
// @param error_value Non-zero to mark as error, zero to clear
DDOG_TRACE_C_API void ddog_trace_span_set_error(ddog_trace_span_t* span_handle,
                                                int error_value);

// Set an error message on a span. No-op if any argument is NULL.
//
// @param span_handle   Span handle
// @param error_message Error message string
DDOG_TRACE_C_API void ddog_trace_span_set_error_message(
    ddog_trace_span_t* span_handle, const char* error_message);

// Inject trace context into outgoing headers via callback.
// No-op if any argument is NULL.
//
// @param span_handle       Span handle
// @param on_context_write  Callback invoked per propagation header
DDOG_TRACE_C_API void ddog_trace_span_inject(
    ddog_trace_span_t* span_handle,
    ddog_trace_context_write_callback on_context_write);

// Create a child span. Returns NULL if any required argument is NULL.
//
// @param span_handle Parent span handle
// @param name        Name of the child span
//
// @return Child span handle, or NULL
DDOG_TRACE_C_API ddog_trace_span_t* ddog_trace_span_create_child(
    ddog_trace_span_t* span_handle, const char* name);

// Finish a span by recording its end time. No-op if span_handle is NULL.
// After finishing, the span should be freed with ddog_trace_span_free.
//
// @param span_handle Span handle
DDOG_TRACE_C_API void ddog_trace_span_finish(ddog_trace_span_t* span_handle);

// Get the trace ID as a zero-padded hex string.
//
// @param span_handle  Span handle
// @param buffer       Output buffer (at least 33 bytes for 128-bit IDs)
// @param buffer_size  Size of the buffer
// @return             Number of characters written, or -1 on error
DDOG_TRACE_C_API int ddog_trace_span_get_trace_id(
    ddog_trace_span_t* span_handle, char* buffer, int buffer_size);

// Get the span ID as a zero-padded hex string.
//
// @param span_handle  Span handle
// @param buffer       Output buffer (at least 17 bytes)
// @param buffer_size  Size of the buffer
// @return             Number of characters written (16), or -1 on error
DDOG_TRACE_C_API int ddog_trace_span_get_span_id(ddog_trace_span_t* span_handle,
                                                 char* buffer, int buffer_size);

// Set the resource name on a span. No-op if any argument is NULL.
//
// @param span_handle Span handle
// @param resource    Resource name
DDOG_TRACE_C_API void ddog_trace_span_set_resource(
    ddog_trace_span_t* span_handle, const char* resource);

// Set the service name on a span. No-op if any argument is NULL.
//
// @param span_handle Span handle
// @param service     Service name
DDOG_TRACE_C_API void ddog_trace_span_set_service(
    ddog_trace_span_t* span_handle, const char* service);

// Set multiple tags at once. No-op if any required argument is NULL or
// count <= 0. Individual entries where key or value is NULL are skipped.
//
// @param span_handle Span handle
// @param keys        Array of tag keys
// @param values      Array of tag values
// @param count       Number of tags
DDOG_TRACE_C_API void ddog_trace_span_set_tags(ddog_trace_span_t* span_handle,
                                               const char** keys,
                                               const char** values, int count);

// Get the sampling priority for the trace this span belongs to.
//
// @param span_handle Span handle
// @param priority    Output: sampling priority value
// @return            1 if a priority was written, 0 if no decision yet,
//                    -1 on error (NULL arguments)
DDOG_TRACE_C_API int ddog_trace_span_get_sampling_priority(
    ddog_trace_span_t* span_handle, int* priority);

// Override the sampling priority for the trace this span belongs to.
// No-op if span_handle is NULL.
//
// @param span_handle Span handle
// @param priority    Sampling priority value
DDOG_TRACE_C_API void ddog_trace_span_set_sampling_priority(
    ddog_trace_span_t* span_handle, int priority);

// Create a child span with explicit service and resource names.
// Returns NULL if span_handle or name is NULL. service and resource may
// be NULL (inherits from parent).
//
// @param span_handle Parent span handle
// @param name        Span name (required)
// @param service     Service name (may be NULL)
// @param resource    Resource name (may be NULL)
//
// @return Child span handle, or NULL
DDOG_TRACE_C_API ddog_trace_span_t* ddog_trace_span_create_child_with_options(
    ddog_trace_span_t* span_handle, const char* name, const char* service,
    const char* resource);

#if defined(__cplusplus)
}
#endif

#endif
