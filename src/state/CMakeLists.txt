# State reconstruction library
add_library(tracesmith-state STATIC
    instruction_stream.cpp
)

target_link_libraries(tracesmith-state PUBLIC
    tracesmith-common
)

target_include_directories(tracesmith-state PUBLIC
    ${CMAKE_SOURCE_DIR}/include
)

set_target_properties(tracesmith-state PROPERTIES
    POSITION_INDEPENDENT_CODE ON
)

# TODO: Add additional state reconstruction components
# state_machine.cpp
# timeline_builder.cpp
