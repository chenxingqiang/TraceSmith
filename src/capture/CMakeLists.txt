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

if(TRACESMITH_ENABLE_MACA)
    message(STATUS "Building MCPTI profiler for MetaX GPUs")
    target_sources(tracesmith-capture PRIVATE mcpti_profiler.cpp)
    
    # Find MACA SDK (typically in /opt/maca)
    if(NOT MACA_ROOT)
        set(MACA_ROOT "/opt/maca" CACHE PATH "Path to MACA SDK")
    endif()
    
    if(EXISTS "${MACA_ROOT}/include/mcpti/mcpti.h")
        message(STATUS "  MACA SDK found at: ${MACA_ROOT}")
        target_include_directories(tracesmith-capture PRIVATE ${MACA_ROOT}/include)
        target_link_directories(tracesmith-capture PRIVATE ${MACA_ROOT}/lib)
        target_link_libraries(tracesmith-capture PUBLIC mcpti mcc)
        target_compile_definitions(tracesmith-capture PUBLIC TRACESMITH_ENABLE_MACA)
    else()
        message(WARNING "MACA SDK not found at ${MACA_ROOT}. Set MACA_ROOT to the correct path.")
    endif()
endif()

if(TRACESMITH_ENABLE_METAL)
    message(STATUS "Building Metal profiler")
    # Objective-C++ source file (use full path to avoid path resolution issues)
    target_sources(tracesmith-capture PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/metal_profiler.mm)
    target_link_libraries(tracesmith-capture PRIVATE 
        "-framework Metal" 
        "-framework Foundation"
    )
    target_compile_definitions(tracesmith-capture PRIVATE TRACESMITH_ENABLE_METAL)
    # Enable Objective-C++ for .mm files
    set_source_files_properties(${CMAKE_CURRENT_SOURCE_DIR}/metal_profiler.mm PROPERTIES
        COMPILE_FLAGS "-fobjc-arc"  # Enable ARC
    )
endif()
