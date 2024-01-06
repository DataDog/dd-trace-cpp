macro(get_WIN32_WINNT version)
  if(CMAKE_SYSTEM_VERSION)
    set(ver ${CMAKE_SYSTEM_VERSION})
    string(REGEX MATCH "^([0-9]+).([0-9])" ver ${ver})
    string(REGEX MATCH "^([0-9]+)" verMajor ${ver})

    # Check for Windows 10, b/c we'll need to convert to hex 'A'.
    if("${verMajor}" MATCHES "10")
      set(verMajor "A")
      string(REGEX REPLACE "^([0-9]+)" ${verMajor} ver ${ver})
    endif()

    string(REPLACE "." "" ver ${ver})
    # Prepend each digit with a zero.
    string(REGEX REPLACE "([0-9A-Z])" "0\\1" ver ${ver})
    set(${version} "0x${ver}")
  endif()
endmacro()

get_WIN32_WINNT(win_ver)
add_compile_definitions(DD_TRACE_PLATFORM_WINDOWS)
add_compile_definitions(_WIN32_WINNT=${win_ver})

if (BUILD_COVERAGE)
  message(FATAL_ERROR "BUILD_COVERAGE is not supported for MSVC build.")
endif ()

if (FUZZ_W3C_PROPAGATION)
  message(FATAL_ERROR "Fuzzers are not support for MSVC build.")
endif ()

if (SANITIZE)
  message(FATAL_ERROR "Sanitize option is not support for MSVC build")
endif ()

# Add project-wide compiler options
add_compile_options(/W4)# /WX)
