# Capture library (profilers)
add_library(tracesmith-capture STATIC
    profiler.cpp
    bpf_tracer.cpp
    memory_profiler.cpp
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
if(TRACESMITH_ENABLE_CUDA AND CUPTI_FOUND)
    message(STATUS "Building CUPTI profiler")
    target_sources(tracesmith-capture PRIVATE cupti_profiler.cpp)
    target_include_directories(tracesmith-capture PRIVATE ${CUPTI_INCLUDE_DIRS})
    # Use PUBLIC so downstream targets also link against CUDA
    target_link_libraries(tracesmith-capture PUBLIC CUPTI::cupti CUDA::cuda_driver)
    target_compile_definitions(tracesmith-capture PUBLIC TRACESMITH_ENABLE_CUDA)
endif()

if(TRACESMITH_ENABLE_ROCM)
    # TODO: Add ROCm profiler
endif()

if(TRACESMITH_ENABLE_METAL)
    message(STATUS "Building Metal profiler")
    # Objective-C++ source file
    target_sources(tracesmith-capture PRIVATE metal_profiler.mm)
    target_link_libraries(tracesmith-capture PRIVATE 
        "-framework Metal" 
        "-framework Foundation"
    )
    target_compile_definitions(tracesmith-capture PRIVATE TRACESMITH_ENABLE_METAL)
    # Enable Objective-C++ for .mm files
    set_source_files_properties(metal_profiler.mm PROPERTIES
        COMPILE_FLAGS "-fobjc-arc"  # Enable ARC
    )
endif()
