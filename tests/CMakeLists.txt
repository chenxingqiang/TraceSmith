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

# GDB RSP backend tests (Unix only)
if(TRACESMITH_BUILD_GDB AND UNIX)
    add_executable(tracesmith_gdb_tests
        test_gdb_rsp.cpp
        test_gpu_debug_engine.cpp
    )
    
    target_include_directories(tracesmith_gdb_tests PRIVATE
        ${googletest_SOURCE_DIR}/googletest/include
    )
    
    target_link_libraries(tracesmith_gdb_tests PRIVATE
        tracesmith-gdb
        gtest_main
        gtest
    )
    
    gtest_discover_tests(tracesmith_gdb_tests)
endif()
