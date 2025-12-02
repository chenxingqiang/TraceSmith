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
    GTest::gtest_main
)

include(GoogleTest)
gtest_discover_tests(tracesmith_tests)
