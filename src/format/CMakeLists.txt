# Format library (SBT file format)
add_library(tracesmith-format STATIC
    sbt_format.cpp
)

target_link_libraries(tracesmith-format PUBLIC
    tracesmith-common
)

target_include_directories(tracesmith-format PUBLIC
    ${CMAKE_SOURCE_DIR}/include
)

set_target_properties(tracesmith-format PROPERTIES
    POSITION_INDEPENDENT_CODE ON
)
