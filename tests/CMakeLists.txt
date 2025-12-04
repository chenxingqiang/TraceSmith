# Unit tests
add_executable(tracesmith_tests
    test_ring_buffer.cpp
    test_sbt_format.cpp
    test_types.cpp
)

target_link_libraries(tracesmith_tests PRIVATE
    tracesmith-common
    tracesmith-format
    tracesmith-capture
    tracesmith-state
    tracesmith-replay
    GTest::gtest_main
)

# Add Perfetto SDK include path if enabled
if(TRACESMITH_USE_PERFETTO_SDK)
    target_link_libraries(tracesmith_tests PRIVATE perfetto_sdk)
endif()

include(GoogleTest)
gtest_discover_tests(tracesmith_tests)
