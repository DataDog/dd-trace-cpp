add_executable(tests
    # test driver
    main.cpp

    # wrapper around Catch2
    test.cpp

    # mocks and utilities
    collectors.cpp
    dict_readers.cpp
    dict_writers.cpp
    event_schedulers.cpp
    http_clients.cpp
    loggers.cpp
    matchers.cpp
    
    # test cases
    test_cerr_logger.cpp
    test_datadog_agent.cpp
    test_glob.cpp
    test_limiter.cpp
    test_smoke.cpp
    test_span.cpp
    test_span_sampler.cpp
    test_trace_segment.cpp
    test_tracer_config.cpp
    test_tracer.cpp
    test_trace_sampler.cpp
)

target_link_libraries(tests dd_trace_cpp ${COVERAGE_LIBRARIES})
if(BUILD_COVERAGE)
    target_link_options(tests PRIVATE -fprofile-arcs -ftest-coverage)
endif()
