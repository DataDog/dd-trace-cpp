#pragma once

#include <stddef.h>
#include <stdint.h>

#if defined(_WIN32)
#if defined(DD_TRACE_C_BUILDING)
#define DD_TRACE_C_API __declspec(dllexport)
#else
#define DD_TRACE_C_API __declspec(dllimport)
#endif
#elif defined(__GNUC__) || defined(__clang__)
#define DD_TRACE_C_API __attribute__((visibility("default")))
#else
#define DD_TRACE_C_API
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
//             dd_tracer_extract_or_create_span returns.
typedef const char* (*dd_context_read_callback)(const char* key);

// Callback used during trace context injection. The tracer calls this
// function for each propagation header it needs to write.
//
// @param key    Header name to set
// @param value  Header value to set
typedef void (*dd_context_write_callback)(const char* key, const char* value);

// Callback invoked with the msgpack-encoded trace payload that would
// otherwise be POSTed to /v0.4/traces. The bytes are identical to the
// HTTP body the tracer would have sent. `user_data` is passed through
// from dd_tracer_conf_set_collector_callback.
//
// Called synchronously on the thread finishing a span; if it blocks,
// all span-finishing on that thread stalls.
typedef void (*dd_trace_msgpack_callback)(const uint8_t* data, size_t size,
                                          void* user_data);

typedef enum {
  DD_OPT_SERVICE_NAME = 0,
  DD_OPT_ENV = 1,
  DD_OPT_VERSION = 2,
  DD_OPT_AGENT_URL = 3,
  DD_OPT_INTEGRATION_NAME = 4,
  DD_OPT_INTEGRATION_VERSION = 5
} dd_tracer_option;

// Options for creating a span. Unset fields default to NULL.
typedef struct {
  const char* name;
  const char* resource;
  const char* service;
  const char* service_type;
  const char* environment;
  const char* version;
} dd_span_options_t;

// Error codes returned by the C binding.
typedef enum {
  DD_ERROR_OK = 0,
  DD_ERROR_NULL_ARGUMENT = 1,
  DD_ERROR_INVALID_CONFIG = 2,
  DD_ERROR_ALLOCATION_FAILURE = 3
} dd_error_code;

// Error details populated on failure.
typedef struct {
  dd_error_code code;
  char message[256];
} dd_error_t;

typedef struct dd_conf_s dd_conf_t;
typedef struct dd_tracer_s dd_tracer_t;
typedef struct dd_span_s dd_span_t;

// Creates a tracer configuration instance.
//
// @return Configuration handle, or NULL on allocation failure
DD_TRACE_C_API dd_conf_t* dd_tracer_conf_new(void);

// Release a tracer configuration. Safe to call with NULL.
//
// @param handle Configuration handle to release
DD_TRACE_C_API void dd_tracer_conf_free(dd_conf_t* handle);

// Set or update a configuration field. No-op if handle is NULL.
//
// @param handle  Configuration handle
// @param option  Configuration option
// @param value   Configuration value (interpretation depends on option)
DD_TRACE_C_API void dd_tracer_conf_set(dd_conf_t* handle,
                                       dd_tracer_option option,
                                       const void* value);

// Install (or clear, by passing NULL) a custom collector callback. When a
// callback is set, finished trace chunks are handed to it instead of being
// POSTed to the Datadog Agent; telemetry and remote-configuration polling
// are also disabled for the resulting tracer.
//
// Caveats:
//   - Telemetry is process-global; if another tracer has already initialized
//     telemetry as enabled in this process, that traffic continues.
//   - DD_TRACE_AGENT_URL, if set to a malformed URL, still fails tracer
//     creation even in callback mode (finalize_config validates the URL
//     before the callback path is taken).
//
// `user_data` must remain valid for the lifetime of any tracer built from
// this handle (the callback may fire during tracer shutdown). No-op if
// handle is NULL.
//
// @param handle    Configuration handle
// @param callback  Callback to receive msgpack payloads, or NULL to clear
// @param user_data Opaque pointer passed verbatim to the callback
DD_TRACE_C_API void dd_tracer_conf_set_collector_callback(
    dd_conf_t* handle, dd_trace_msgpack_callback callback, void* user_data);

// Creates a tracer instance. The configuration handle may be freed with
// dd_tracer_conf_free after this call returns.
//
// @param conf_handle Configuration handle (not modified)
// @param error       If non-NULL, filled with error details on failure.
//                    The caller must allocate the dd_error_t struct.
//
// @return Tracer handle, or NULL on error
DD_TRACE_C_API dd_tracer_t* dd_tracer_new(const dd_conf_t* conf_handle,
                                          dd_error_t* error);

// Release a tracer instance. Safe to call with NULL.
//
// @param tracer_handle Tracer handle to release
DD_TRACE_C_API void dd_tracer_free(dd_tracer_t* tracer_handle);

// Create a span using a Tracer.
//
// @param tracer_handle Tracer handle
// @param options       Span options (name must not be NULL)
//
// @return Span handle, or NULL on error
DD_TRACE_C_API dd_span_t* dd_tracer_create_span(dd_tracer_t* tracer_handle,
                                                dd_span_options_t options);

// Extract trace context from incoming headers, or create a new root span
// if extraction fails. Never returns an error span; on extraction failure
// a fresh root span is created.
//
// @param tracer_handle    Tracer handle
// @param on_context_read  Callback invoked to read propagation headers
// @param options          Span options (name must not be NULL)
//
// @return Span handle, or NULL if arguments are invalid
DD_TRACE_C_API dd_span_t* dd_tracer_extract_or_create_span(
    dd_tracer_t* tracer_handle, dd_context_read_callback on_context_read,
    dd_span_options_t options);

// Release a span instance. Safe to call with NULL.
// If the span has not been finished with dd_span_finish, it is
// automatically finished (its end time is recorded) before being freed.
//
// @param span_handle Span handle
DD_TRACE_C_API void dd_span_free(dd_span_t* span_handle);

// Set a tag (key-value pair) on a span. No-op if any argument is NULL.
//
// @param span_handle Span handle
// @param key         Tag key
// @param value       Tag value
DD_TRACE_C_API void dd_span_set_tag(dd_span_t* span_handle, const char* key,
                                    const char* value);

// Mark a span as erroneous. No-op if span_handle is NULL.
//
// @param span_handle Span handle
// @param error_value Non-zero to mark as error, zero to clear
DD_TRACE_C_API void dd_span_set_error(dd_span_t* span_handle, int error_value);

// Set an error message on a span. No-op if any argument is NULL.
//
// @param span_handle   Span handle
// @param error_message Error message string
DD_TRACE_C_API void dd_span_set_error_message(dd_span_t* span_handle,
                                              const char* error_message);

// Inject trace context into outgoing headers via callback.
// No-op if any argument is NULL.
//
// @param span_handle       Span handle
// @param on_context_write  Callback invoked per propagation header
DD_TRACE_C_API void dd_span_inject(dd_span_t* span_handle,
                                   dd_context_write_callback on_context_write);

// Create a child span. Returns NULL if any required argument is NULL.
//
// @param span_handle Parent span handle
// @param options     Span options (name must not be NULL)
//
// @return Child span handle, or NULL
DD_TRACE_C_API dd_span_t* dd_span_create_child(dd_span_t* span_handle,
                                               dd_span_options_t options);

// Finish a span by recording its end time. No-op if span_handle is NULL.
// After finishing, the span should be freed with dd_span_free.
//
// @param span_handle Span handle
DD_TRACE_C_API void dd_span_finish(dd_span_t* span_handle);

// Get the trace ID as a zero-padded hex string.
//
// @param span_handle  Span handle
// @param buffer       Output buffer (at least 33 bytes for 128-bit IDs)
// @param buffer_size  Size of the buffer
// @return             Number of characters written, or -1 on error
DD_TRACE_C_API int dd_span_get_trace_id(dd_span_t* span_handle, char* buffer,
                                        size_t buffer_size);

// Get the span ID as a zero-padded hex string.
//
// @param span_handle  Span handle
// @param buffer       Output buffer (at least 17 bytes)
// @param buffer_size  Size of the buffer
// @return             Number of characters written (16), or -1 on error
DD_TRACE_C_API int dd_span_get_span_id(dd_span_t* span_handle, char* buffer,
                                       size_t buffer_size);

// Set the resource name on a span. No-op if any argument is NULL.
//
// @param span_handle Span handle
// @param resource    Resource name
DD_TRACE_C_API void dd_span_set_resource(dd_span_t* span_handle,
                                         const char* resource);

// Set the service name on a span. No-op if any argument is NULL.
//
// @param span_handle Span handle
// @param service     Service name
DD_TRACE_C_API void dd_span_set_service(dd_span_t* span_handle,
                                        const char* service);

#if defined(__cplusplus)
}
#endif
