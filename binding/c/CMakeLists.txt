add_library(dd_trace_c)
add_library(dd-trace-cpp::c_binding ALIAS dd_trace_c)

target_compile_definitions(dd_trace_c PRIVATE DD_TRACE_C_BUILDING)

target_sources(dd_trace_c
  PRIVATE
    $<TARGET_OBJECTS:dd-trace-cpp::obj>
    src/tracer.cpp
)

if (DD_TRACE_TRANSPORT STREQUAL "curl")
  target_sources(dd_trace_c
    PRIVATE
      ${CMAKE_SOURCE_DIR}/src/datadog/curl.cpp
      ${CMAKE_SOURCE_DIR}/src/datadog/default_http_client_curl.cpp
  )

  target_link_libraries(dd_trace_c
    PRIVATE
      CURL::libcurl_shared
  )
else ()
  target_sources(dd_trace_c
    PRIVATE
      ${CMAKE_SOURCE_DIR}/src/datadog/default_http_client_null.cpp
  )
endif ()

target_include_directories(dd_trace_c
  PUBLIC
    $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
  PRIVATE
    ${CMAKE_SOURCE_DIR}/src
)

target_link_libraries(dd_trace_c
  PRIVATE
    dd-trace-cpp::obj
    dd-trace-cpp::specs
)

install(TARGETS dd_trace_c
  EXPORT dd-trace-cpp-targets
  LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
  ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
  RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)

install(
  DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/include/datadog
  DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
)

if (DD_TRACE_BUILD_TESTING)
  target_sources(tests PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/test/test_c_binding.cpp
  )

  target_include_directories(tests PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/include
  )

  target_link_libraries(tests PRIVATE dd_trace_c)
endif ()
