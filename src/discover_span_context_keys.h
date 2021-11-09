#pragma once

extern "C" {
#include <nginx.h>
#include <ngx_config.h>
#include <ngx_core.h>
#include <ngx_http.h>
}

namespace datadog {
namespace nginx {
//------------------------------------------------------------------------------
// discover_span_context_keys
//------------------------------------------------------------------------------
ngx_array_t* discover_span_context_keys(ngx_pool_t* pool, ngx_log_t* log,
                                        const char* tracing_library,
                                        const char* tracer_config_file);
}  // namespace nginx
}  // namespace datadog