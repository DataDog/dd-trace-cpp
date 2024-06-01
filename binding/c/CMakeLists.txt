add_library(dd_trace_c SHARED)
add_library(dd_trace::c_binding ALIAS dd_trace_c)

add_dependencies(dd_trace_c dd_trace::obj)

target_sources(dd_trace_c
  PRIVATE
    $<TARGET_OBJECTS:dd_trace::obj>
    src/tracer.cpp
)

# Headers location are different depending of whether we are building 
# or installing the library.
target_include_directories(dd_trace_c
  PUBLIC
    $<INSTALL_INTERFACE:include/c>
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
)

target_link_libraries(dd_trace_c
  PUBLIC
    dd_trace::obj
  PRIVATE
    dd_trace::specs
)

install(TARGETS dd_trace_c dd_trace_cpp-objects
  EXPORT dd_trace_cpp-export
  FILE_SET public_headers DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
  LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
  ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
  # For Windows
  RUNTIME DESTINATION ${CMAKE_INSTALL_LIBDIR}
)
