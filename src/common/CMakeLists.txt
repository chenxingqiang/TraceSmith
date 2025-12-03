# Common library
add_library(tracesmith-common STATIC
    types.cpp
    stack_capture.cpp
    xray_importer.cpp
)

target_include_directories(tracesmith-common PUBLIC
    ${CMAKE_SOURCE_DIR}/include
)

set_target_properties(tracesmith-common PROPERTIES
    POSITION_INDEPENDENT_CODE ON
)

# Link libunwind if available
if(TRACESMITH_USE_LIBUNWIND AND LIBUNWIND_FOUND)
    target_link_libraries(tracesmith-common PRIVATE Libunwind::libunwind)
    target_compile_definitions(tracesmith-common PRIVATE TRACESMITH_USE_LIBUNWIND)
endif()
