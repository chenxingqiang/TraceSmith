# State reconstruction library
add_library(tracesmith-state STATIC
    instruction_stream.cpp
    gpu_state_machine.cpp
    timeline_builder.cpp
    perfetto_exporter.cpp
    perfetto_proto_exporter.cpp
    timeline_viewer.cpp
)

target_link_libraries(tracesmith-state PUBLIC
    tracesmith-common
)

# Link Perfetto SDK if enabled
if(TRACESMITH_USE_PERFETTO_SDK)
    target_link_libraries(tracesmith-state PUBLIC perfetto_sdk)
endif()

target_include_directories(tracesmith-state PUBLIC
    ${CMAKE_SOURCE_DIR}/include
)

set_target_properties(tracesmith-state PROPERTIES
    POSITION_INDEPENDENT_CODE ON
)

# TODO: Add additional state reconstruction components
# state_machine.cpp
# timeline_builder.cpp
