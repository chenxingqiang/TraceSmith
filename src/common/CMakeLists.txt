# Common library
add_library(tracesmith-common STATIC
    types.cpp
    stack_capture.cpp
)

target_include_directories(tracesmith-common PUBLIC
    ${CMAKE_SOURCE_DIR}/include
)

set_target_properties(tracesmith-common PROPERTIES
    POSITION_INDEPENDENT_CODE ON
)
