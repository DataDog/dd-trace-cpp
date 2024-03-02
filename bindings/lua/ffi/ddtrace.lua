local ffi = require("ffi")

ffi.cdef [[
  int return_one_two_four();
  void* make_tracer();
  void* create_span(void*);
  void span_gc(void*);
  void tracer_gc(void*);
  void set_span(void*, const char* const, const char* const);
]]

local lib_ddtrace = ffi.load("ddtrace.so")

local tracer = ffi.gc(lib_ddtrace.make_tracer(), lib_ddtrace.tracer_gc)
local span = ffi.gc(lib_ddtrace.create_span(tracer), lib_ddtrace.span_gc)
lib_ddtrace.set_span(span, "team", "apm-proxy")
