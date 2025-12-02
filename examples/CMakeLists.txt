# Examples

add_executable(basic_example
    basic_example.cpp
)

target_link_libraries(basic_example PRIVATE
    tracesmith-common
    tracesmith-format
    tracesmith-capture
)

add_executable(phase2_example
    phase2_example.cpp
)

target_link_libraries(phase2_example PRIVATE
    tracesmith-common
    tracesmith-format
    tracesmith-capture
    tracesmith-state
)

add_executable(phase3_example
    phase3_example.cpp
)

target_link_libraries(phase3_example PRIVATE
    tracesmith-common
    tracesmith-format
    tracesmith-capture
    tracesmith-state
)
