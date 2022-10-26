add_executable(tests
    # test driver
    main.cpp

    # wrapper around Catch2
    test.cpp

    # mocks
    mocks/collectors.cpp
    mocks/dict_readers.cpp
    mocks/dict_writers.cpp
    mocks/event_schedulers.cpp
    mocks/http_clients.cpp
    mocks/loggers.cpp
    
    # utilities
    matchers.cpp
    
    # test cases
    cerr_logger.cpp
    datadog_agent.cpp
    glob.cpp
    limiter.cpp
    smoke.cpp
    span.cpp
    span_sampler.cpp
    trace_segment.cpp
    tracer_config.cpp
    tracer.cpp
    trace_sampler.cpp
)

target_link_libraries(tests dd_trace_cpp ${COVERAGE_LIBRARIES})
if(BUILD_COVERAGE)
    target_link_options(tests PRIVATE -fprofile-arcs -ftest-coverage)
endif()
