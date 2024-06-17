#ifndef DD_TRACE_SDK_C_TRACER_H
#define DD_TRACE_SDK_C_TRACER_H

#include <stddef.h>

#if defined(__cplusplus)
extern "C" {
#endif

struct str_view {
  const char* buf;
  size_t len;
};

// TBD
//
// @param       key         Context key to read
// @param[out]  result      TBD
typedef str_view (*datadog_sdk_context_read_callback)(void* reader_ctx,
                                                      str_view key);

// TBD
//
// @param[out] key        Key to write
// @param[out] value      Value
typedef void (*datadog_sdk_context_write_callback)(void* writer_ctx,
                                                   str_view key,
                                                   str_view value);

enum datadog_sdk_tracer_option {
  DATADOG_TRACER_OPT_SERVICE_NAME = 0,
  DATADOG_TRACER_OPT_ENV = 1,
  DATADOG_TRACER_OPT_VERSION = 2,
  DATADOG_TRACER_OPT_AGENT_URL = 3,
  DATADOG_TRACER_OPT_LIBRARY_VERSION = 4,
  DATADOG_TRACER_OPT_LIBRARY_LANGUAGE = 5,
  DATADOG_TRACER_OPT_LIBRARY_LANGUAGE_VERSION = 6,
};

typedef void datadog_sdk_conf_t;

/* Creates a tracer configuration instance.
 *
 * @return tracer handle, or a null pointer on error
 */
datadog_sdk_conf_t* datadog_sdk_tracer_conf_new();

// Release a tracer configuration.
//
// @param handle Configuration handle to release
void datadog_sdk_tracer_conf_free(datadog_sdk_conf_t* handle);

// Set or update a configuration field.
//
// @param handle  Configuration handle
// @param option  Configuration option
// @param value   Configuration value
void datadog_sdk_tracer_conf_set(datadog_sdk_conf_t* handle,
                                 enum datadog_sdk_tracer_option option,
                                 void* value);

// Tracer
typedef void datadog_sdk_tracer_t;
typedef void datadog_sdk_span_t;

// Creates a tracer instance.
//
// @param conf_handle Configuration handle for configuring the tracer
//
// @return tracer_handle Tracer handle, or a null pointer on error
datadog_sdk_tracer_t* datadog_sdk_tracer_new(datadog_sdk_conf_t* conf_handle);

// Release a tracer instance
//
// @param tracer_handle Tracer handle to release
void datadog_sdk_tracer_free(datadog_sdk_tracer_t* tracer_handle);

// Create a span using a Tracer.
//
// @param   tracer_handle Tracer handle use to create the span
// @param   name          Name of the span
//
// @return  span_handle   Span handle, or a null pointer on error
datadog_sdk_span_t* datadog_sdk_tracer_create_span(
    datadog_sdk_tracer_t* tracer_handle, str_view name);

// Extract or create a span using a Tracer.
//
// @param tracer_handle Tracer handle used to create the span
// @param on_context_read Callback that will be called during context
// propagation
// @param name            Name to associate on the span
// @param resource        Resource to associate on the span
//
// @return span_handle    Span handle, or a null pointer on error
datadog_sdk_span_t* datadog_sdk_tracer_extract_or_create_span(
    datadog_sdk_tracer_t* tracer_handle, void* reader_ctx,
    datadog_sdk_context_read_callback on_context_read, str_view name,
    str_view resource);

// Release a span instance
//
// @param span_handle Handle on a span
void datadog_sdk_span_free(datadog_sdk_span_t* span_handle);

// Set a tag on a span
//
// A tag is a key-value associate on a span
//
// @param span_handle Handle on span instance
// @param key         Key to associate
// @param value       Value
void datadog_sdk_span_set_tag(datadog_sdk_span_t* span_handle, str_view key,
                              str_view value);

// Set type
//
// @param span_handle   Handle on a span instance.
// @param name          Name of the span.
//
// @return span_handle  Handle or a null pointer
void datadog_sdk_span_set_type(datadog_sdk_span_t* span_handle,
                                    str_view type);


// Set a span as errorneous
//
// @param span_handle Handle on a span instance
// @param error_value Boolean value
void datadog_sdk_span_set_error(datadog_sdk_span_t* span_handle,
                                int error_value);

// Set error message on a span
//
// @param span_handle   Handle on a span instance
// @param error_message Error message
void datadog_sdk_span_set_error_message(datadog_sdk_span_t* span_handle,
                                        str_view error_message);

// Context propagation injection
//
// @param span_handle       Handle on a span instance
// @param on_context_write  Callback that will be called per field for context
// propagation
void datadog_sdk_span_inject(
    datadog_sdk_span_t* span_handle, void* writer_ctx,
    datadog_sdk_context_write_callback on_context_write);

// Create a child span.
//
// @param span_handle   Handle on a span instance.
// @param name          Name of the span.
//
// @return span_handle  Handle or a null pointer
void* datadog_sdk_span_create_child(datadog_sdk_span_t* span_handle,
                                    str_view name);

// Stop the span time
//
// @param span_handle   Handle on a span instance.
void datadog_sdk_span_finish(datadog_sdk_span_t* span_handle);

#if defined(__cplusplus)
}
#endif

#endif
