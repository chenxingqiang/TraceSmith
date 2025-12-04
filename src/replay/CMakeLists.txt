# Replay engine library
add_library(tracesmith-replay STATIC
    stream_scheduler.cpp
    operation_executor.cpp
    determinism_checker.cpp
    replay_engine.cpp
    frame_capture.cpp
)

target_link_libraries(tracesmith-replay PUBLIC
    tracesmith-common
    tracesmith-format
    tracesmith-state
)

target_include_directories(tracesmith-replay PUBLIC
    ${CMAKE_SOURCE_DIR}/include
)

set_target_properties(tracesmith-replay PROPERTIES
    POSITION_INDEPENDENT_CODE ON
)
