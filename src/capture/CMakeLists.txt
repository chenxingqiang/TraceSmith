# Capture library (profilers)
add_library(tracesmith-capture STATIC
    profiler.cpp
)

target_link_libraries(tracesmith-capture PUBLIC
    tracesmith-common
)

target_include_directories(tracesmith-capture PUBLIC
    ${CMAKE_SOURCE_DIR}/include
)

set_target_properties(tracesmith-capture PROPERTIES
    POSITION_INDEPENDENT_CODE ON
)

# Platform-specific profiler implementations
if(TRACESMITH_ENABLE_CUDA)
    # TODO: Add CUPTI profiler
    # target_sources(tracesmith-capture PRIVATE cupti_profiler.cpp)
    # find_package(CUDA REQUIRED)
    # target_link_libraries(tracesmith-capture PRIVATE ${CUDA_LIBRARIES} cupti)
endif()

if(TRACESMITH_ENABLE_ROCM)
    # TODO: Add ROCm profiler
endif()

if(TRACESMITH_ENABLE_METAL)
    # TODO: Add Metal profiler
endif()
