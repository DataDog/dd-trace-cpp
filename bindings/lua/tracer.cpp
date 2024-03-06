#ifdef __cplusplus
#include <datadog/dict_reader.h>
#include <datadog/dict_writer.h>
#include <datadog/tracer.h>

#include <iostream>
#include <queue>
#include <thread>

#include "lua.hpp"
#else
#include "lauxlib.h"
#include "lua.h"
#include "lualib.h"
#endif
#include <math.h>

// so that name mangling doesn't mess up function names
#ifdef __cplusplus
extern "C" {
#endif

namespace dd = datadog::tracing;

static std::string get_lua_version(lua_State *L) {
  return std::to_string(lua_version(L));
}

static int make_tracer(lua_State *L) {
  luaL_checktype(L, 1, LUA_TTABLE);

  const auto lua_version = get_lua_version(L);

  dd::TracerConfig config;
  config.defaults.tags.emplace("bindings.language", "lua");
  config.defaults.tags.emplace("bindings.version", lua_version);
  config.integration_name = "lua";
  config.integration_version = lua_version;

  // Iterate over the table
  lua_pushnil(L);                // Push nil as the initial key
  while (lua_next(L, 1) != 0) {  // Pushes key-value pair onto the stack
    // Now, key is at index -2, and value is at index -1
    std::string_view key{lua_tostring(L, -2)};
    std::string_view value{lua_tostring(L, -1)};
    if (key == "version") {
      config.defaults.version = value;
    } else if (key == "env") {
      config.defaults.environment = value;
    } else if (key == "service") {
      config.defaults.service = value;
    }
    // printf("Key: ");
    // print_lua_value(L, -2);  // Print the key
    // printf("Value: ");
    // print_lua_value(L, -1);  // Print the value
    // Removes value, keeps key for next iteration
    lua_pop(L, 1);
  }

  const auto validated_config = dd::finalize_config(config);
  // if (!validated_config) {
  //   error(L, "cannot run configuration file: %s", lua_tostring(L, -1));
  //   std::cerr << validated_config.error() << '\n';
  //   return 1;
  // }

  void *buffer = lua_newuserdata(L, sizeof(dd::Tracer));
  new (buffer) dd::Tracer{*validated_config};

  luaL_getmetatable(L, "ddtrace.tracer");
  lua_setmetatable(L, -2);

  return 1;
}

static dd::Tracer *checktracer(lua_State *L) {
  void *ud = luaL_checkudata(L, 1, "ddtrace.tracer");
  luaL_argcheck(L, ud != NULL, 1, "`tracer` expected");
  return (dd::Tracer *)ud;
}

static int tracer_gc(lua_State *L) {
  std::cout << "ending tracer\n";
  dd::Tracer *tracer = (dd::Tracer *)lua_touserdata(L, 1);
  // delete tracer;
  tracer->~Tracer();
  return 0;
}

static int create_span(lua_State *L) {
  dd::Tracer *tracer = checktracer(L);
  const char *const name = luaL_checkstring(L, 2);

  luaL_argcheck(L, tracer != NULL, 1, "`tracer` expected");

  dd::Span span = tracer->create_span();
  span.set_name(name);
  // std::this_thread::sleep_for(std::chrono::seconds(1));

  void *buffer = lua_newuserdata(L, sizeof(dd::Span));
  new (buffer) dd::Span(std::move(span));

  luaL_getmetatable(L, "ddtrace.span");
  lua_setmetatable(L, -2);

  return 1;
}

static int span_gc(lua_State *L) {
  dd::Span *span = (dd::Span *)lua_touserdata(L, 1);
  std::cout << "[GC] Span: " << span << "\n";
  std::cout << "name: " << span->name() << "\n";
  span->~Span();
  // delete span;
  return 0;
}

static dd::Span *checkspan(lua_State *L) {
  void *ud = luaL_checkudata(L, 1, "ddtrace.span");
  luaL_argcheck(L, ud != NULL, 1, "`span` expected");
  return (dd::Span *)ud;
}

static int finish(lua_State *L) {
  dd::Span *span = checkspan(L);
  span->set_end_time(std::chrono::steady_clock::now());
  // span->~Span();
  return 0;
}

static int create_child(lua_State *L) {
  dd::Span *span = checkspan(L);

  const char *const name = luaL_checkstring(L, 2);

  dd::Span child_span = span->create_child();
  child_span.set_name(name);

  void *buffer = lua_newuserdata(L, sizeof(dd::Span));
  new (buffer) dd::Span(std::move(child_span));

  luaL_getmetatable(L, "ddtrace.span");
  lua_setmetatable(L, -2);

  return 1;
}

static int set_tag(lua_State *L) {
  dd::Span *span = checkspan(L);

  dd::StringView key = luaL_checkstring(L, 2);
  dd::StringView value = luaL_checkstring(L, 3);

  span->set_tag(key, value);
  return 0;
}

static int set_error(lua_State *L) {
  dd::Span *span = checkspan(L);

  bool error = luaL_checkinteger(L, 2) != 0;
  span->set_error(error);

  return 0;
}

class LuaReader : public datadog::tracing::DictReader {
 private:
  lua_State *L_;

 public:
  LuaReader(lua_State *L) : L_(L) {}
  ~LuaReader() = default;

  dd::Optional<dd::StringView> lookup(dd::StringView key) const override {
    lua_pushvalue(L_, 2);  // Push the function onto the stack
    lua_pushstring(L_, key.data());
    // Call the function with 1 argument and 0 return values
    lua_call(L_, 1, 1);

    if (lua_isnil(L_, -1)) {
      return dd::nullopt;
    }

    std::string_view value{lua_tostring(L_, -1)};
    std::cout << "Retrieved: " << value << "\n";
    return value;
  };

  // Invoke the specified `visitor` once for each key/value pair in this object.
  void visit(const std::function<void(dd::StringView key, dd::StringView value)>
                 & /* visitor */) const override{};
};

static int extract_span(lua_State *L) {
  dd::Tracer *tracer = checktracer(L);

  luaL_checktype(L, 2, LUA_TFUNCTION);

  LuaReader reader(L);
  auto maybe_span = tracer->extract_span(reader);
  if (auto error = maybe_span.if_error()) {
    return luaL_error(L, error->message.c_str());
  }

  return 0;
}

class LuaWriter : public dd::DictWriter {
  lua_State *L_;

 public:
  LuaWriter(lua_State *L) : L_(L) {}
  ~LuaWriter() = default;

  void set(dd::StringView key, dd::StringView value) override {
    lua_pushvalue(L_, 2);  // Push the function onto the stack
    lua_pushstring(L_, key.data());
    lua_pushstring(L_, value.data());
    // Call the function with 2 arguments and 0 return values
    lua_call(L_, 2, 0);
  };
};

static int inject_span(lua_State *L) {
  dd::Span *span = checkspan(L);
  luaL_checktype(L, 2, LUA_TFUNCTION);

  LuaWriter writer(L);
  span->inject(writer);
  return 0;
}

// library to be registered
// clang-format off
static const struct luaL_Reg ddtracelib[] = {
    {"make_tracer", make_tracer},
    {NULL, NULL}       /* sentinel */
};

static const struct luaL_Reg tracer_methods[] = {
    // {"__tostring", array2string},
    {"__gc", tracer_gc},
    {"create_span", create_span},
    {"extract_span", extract_span},
    {NULL, NULL}       /* sentinel */
};

static const struct luaL_Reg span_methods[] = {
    // {"__tostring", array2string},
    {"__gc", span_gc},
    {"finish", finish},
    {"create_child", create_child},
    {"inject", inject_span},
    {"set_tag", set_tag},
    {"set_error", set_error},
    {NULL, NULL}       /* sentinel */
};
// clang-format on

// name of this function is not flexible
int luaopen_ddtrace(lua_State *L) {
  luaL_newmetatable(L, "ddtrace.span");
  lua_pushstring(L, "__index");
  lua_pushvalue(L, -2); /* pushes the metatable */
  lua_settable(L, -3);  /* metatable.__index = metatable */
  luaL_setfuncs(L, span_methods, 0);

  luaL_newmetatable(L, "ddtrace.tracer");
  lua_pushstring(L, "__index");
  lua_pushvalue(L, -2); /* pushes the metatable */
  lua_settable(L, -3);  /* metatable.__index = metatable */
  luaL_setfuncs(L, tracer_methods, 0);

  luaL_newlib(L, ddtracelib);

  return 1;
}

#ifdef __cplusplus
}
#endif
