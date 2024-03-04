local ffi = require("ffi")

ffi.cdef [[
  typedef const char* (*ReaderFunc)(const char*);
  typedef void (*WriterFunc)(const char*, const char*);

  // Tracer
  void* tracer_new();
  void tracer_free(void*);
  void* tracer_create_span(void*, const char*);
  void* tracer_extract_or_create_span(void*, ReaderFunc);

  // Span
  void span_free(void*);
  void span_set_tag(void*, const char*, const char*);
  void span_inject(void*, WriterFunc);
  void* span_create_child(void*, const char*);
  void span_finish(void*);
]]

local lib_ddtrace = ffi.load("ddtrace.so")

local function make_tracer()
  return ffi.gc(lib_ddtrace.tracer_new(), lib_ddtrace.tracer_free)
end

local function create_span(tracer, name)
  return ffi.gc(lib_ddtrace.tracer_create_span(tracer, name), lib_ddtrace.span_free)
end

local function set_tag(span, key, value)
  lib_ddtrace.span_set_tag(span, key, value)
end

local function extract_or_create_span(tracer, reader_cb)
  return ffi.gc(lib_ddtrace.tracer_extract_or_create_span(tracer, reader_cb), lib_ddtrace.span_free)
end

local function inject_span(span, writer_cb)
  lib_ddtrace.span_inject(span, writer_cb)
end

local function create_child(span, name)
  return ffi.gc(lib_ddtrace.span_create_child(span, name), lib_ddtrace.span_free)
end

local function finish_span(span)
  lib_ddtrace.span_finish(span)
end

local tracer = make_tracer()
local span = create_span(tracer, "root-span")
set_tag(span, "team", "apm-proxy")
set_tag(span, "user", "dmehala")

local lua_reader = function(key)
  print("[reader] key: " .. ffi.string(key))
  -- return "a"
  return nil
end

local lua_writer = function(key, value)
  -- print("hello")
  print("[writer] key: " .. ffi.string(key) .. ", value: " .. ffi.string(value))
end

local child = create_child(span, "child_span")
finish_span(child)
finish_span(span)

-- local extracted_span = extract_or_create_span(tracer, lua_reader)
-- inject_span(extracted_span, lua_writer)
