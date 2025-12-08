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
    
    # Find MACA SDK (try multiple locations)
    if(NOT MACA_ROOT)
        # Try common locations
        foreach(MACA_PATH "/opt/maca-3.0.0" "/opt/maca" "/usr/local/maca")
            if(EXISTS "${MACA_PATH}/include/mcpti/mcpti.h")
                set(MACA_ROOT "${MACA_PATH}" CACHE PATH "Path to MACA SDK")
                break()
            endif()
        endforeach()
    endif()
    
    if(MACA_ROOT AND EXISTS "${MACA_ROOT}/include/mcpti/mcpti.h")
        message(STATUS "  MACA SDK found at: ${MACA_ROOT}")
        # Use PUBLIC so downstream targets can include mcpti_profiler.hpp
        # which requires <mcr/maca.h>, <mcr/mc_runtime_api.h>, <mcpti/mcpti.h>
        target_include_directories(tracesmith-capture PUBLIC 
            ${MACA_ROOT}/include
            ${MACA_ROOT}/include/mcr
            ${MACA_ROOT}/include/mcpti
        )
        
        # Find and link MACA libraries with full path
        find_library(MCPTI_LIBRARY mcpti PATHS ${MACA_ROOT}/lib ${MACA_ROOT}/lib64 NO_DEFAULT_PATH)
        find_library(MCRUNTIME_LIBRARY mcruntime PATHS ${MACA_ROOT}/lib ${MACA_ROOT}/lib64 NO_DEFAULT_PATH)
        
        if(MCPTI_LIBRARY AND MCRUNTIME_LIBRARY)
            message(STATUS "  MCPTI library: ${MCPTI_LIBRARY}")
            message(STATUS "  MCRuntime library: ${MCRUNTIME_LIBRARY}")
            target_link_libraries(tracesmith-capture PUBLIC ${MCPTI_LIBRARY} ${MCRUNTIME_LIBRARY})
            target_compile_definitions(tracesmith-capture PUBLIC TRACESMITH_ENABLE_MACA)
        else()
            message(WARNING "MACA libraries not found")
            set(TRACESMITH_ENABLE_MACA OFF PARENT_SCOPE)
        endif()
    else()
        message(WARNING "MACA SDK not found. Set MACA_ROOT to the correct path.")
        set(TRACESMITH_ENABLE_MACA OFF PARENT_SCOPE)
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
