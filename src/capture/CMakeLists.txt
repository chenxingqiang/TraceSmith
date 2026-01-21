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
    message(STATUS "Building ROCm profiler for AMD GPUs")
    target_sources(tracesmith-capture PRIVATE rocm_profiler.cpp)
    
    # Find ROCm SDK
    if(NOT ROCM_PATH)
        # Try common locations
        foreach(ROCM_DIR "/opt/rocm" "/opt/rocm-6.0.0" "/opt/rocm-5.7.0")
            if(EXISTS "${ROCM_DIR}/include/hip/hip_runtime.h")
                set(ROCM_PATH "${ROCM_DIR}" CACHE PATH "Path to ROCm SDK")
                break()
            endif()
        endforeach()
    endif()
    
    if(ROCM_PATH AND EXISTS "${ROCM_PATH}/include/hip/hip_runtime.h")
        message(STATUS "  ROCm SDK found at: ${ROCM_PATH}")
        
        # Include directories
        target_include_directories(tracesmith-capture PUBLIC 
            ${ROCM_PATH}/include
            ${ROCM_PATH}/include/hip
            ${ROCM_PATH}/include/roctracer
        )
        
        # Find ROCm libraries
        find_library(HIP_LIBRARY amdhip64 
            PATHS ${ROCM_PATH}/lib ${ROCM_PATH}/lib64
            NO_DEFAULT_PATH)
        find_library(ROCTRACER_LIBRARY roctracer64
            PATHS ${ROCM_PATH}/lib ${ROCM_PATH}/lib64
            NO_DEFAULT_PATH)
        find_library(ROCTX_LIBRARY roctx64
            PATHS ${ROCM_PATH}/lib ${ROCM_PATH}/lib64
            NO_DEFAULT_PATH)
        
        if(HIP_LIBRARY AND ROCTRACER_LIBRARY)
            message(STATUS "  HIP library: ${HIP_LIBRARY}")
            message(STATUS "  roctracer library: ${ROCTRACER_LIBRARY}")
            
            target_link_libraries(tracesmith-capture PUBLIC 
                ${HIP_LIBRARY} 
                ${ROCTRACER_LIBRARY}
            )
            
            if(ROCTX_LIBRARY)
                message(STATUS "  roctx library: ${ROCTX_LIBRARY}")
                target_link_libraries(tracesmith-capture PUBLIC ${ROCTX_LIBRARY})
            endif()
            
            # Add rpath for runtime
            set_target_properties(tracesmith-capture PROPERTIES
                BUILD_RPATH "${ROCM_PATH}/lib;${ROCM_PATH}/lib64"
                INSTALL_RPATH "${ROCM_PATH}/lib;${ROCM_PATH}/lib64"
            )
            
            target_compile_definitions(tracesmith-capture PUBLIC TRACESMITH_ENABLE_ROCM)
        else()
            message(WARNING "ROCm libraries not found")
            set(TRACESMITH_ENABLE_ROCM OFF PARENT_SCOPE)
        endif()
    else()
        message(WARNING "ROCm SDK not found. Set ROCM_PATH to the correct path.")
        set(TRACESMITH_ENABLE_ROCM OFF PARENT_SCOPE)
    endif()
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

if(TRACESMITH_ENABLE_ASCEND)
    message(STATUS "Building Ascend profiler for Huawei NPUs")
    target_sources(tracesmith-capture PRIVATE ascend_profiler.cpp)
    
    # Find Ascend SDK
    if(NOT ASCEND_HOME)
        foreach(ASCEND_PATH "/usr/local/Ascend/ascend-toolkit/latest" "/usr/local/Ascend")
            if(EXISTS "${ASCEND_PATH}/include/acl/acl.h")
                set(ASCEND_HOME "${ASCEND_PATH}" CACHE PATH "Path to Ascend CANN SDK")
                break()
            endif()
        endforeach()
    endif()
    
    if(ASCEND_HOME AND EXISTS "${ASCEND_HOME}/include/acl/acl.h")
        message(STATUS "  Ascend SDK found at: ${ASCEND_HOME}")
        
        # Include directories
        target_include_directories(tracesmith-capture PUBLIC 
            ${ASCEND_HOME}/include
        )
        
        # Determine library path based on architecture
        if(CMAKE_SYSTEM_PROCESSOR MATCHES "aarch64")
            set(ASCEND_LIB_PATH ${ASCEND_HOME}/aarch64-linux/lib64)
        else()
            set(ASCEND_LIB_PATH ${ASCEND_HOME}/lib64)
        endif()
        
        # Find ACL library
        find_library(ACL_LIBRARY ascendcl 
            PATHS ${ASCEND_LIB_PATH} ${ASCEND_HOME}/lib64
            NO_DEFAULT_PATH)
        
        # Find profiler library
        find_library(MSPROFILER_LIBRARY msprofiler
            PATHS ${ASCEND_LIB_PATH} ${ASCEND_HOME}/lib64
            NO_DEFAULT_PATH)
        
        if(ACL_LIBRARY)
            message(STATUS "  ACL library: ${ACL_LIBRARY}")
            
            # Find driver library path
            set(ASCEND_DRIVER_LIB_PATH "/usr/local/Ascend/driver/lib64/driver")
            
            # Add link directories for runtime library search (including driver)
            target_link_directories(tracesmith-capture PUBLIC 
                ${ASCEND_LIB_PATH}
                ${ASCEND_HOME}/lib64
                ${ASCEND_DRIVER_LIB_PATH}
            )
            
            target_link_libraries(tracesmith-capture PUBLIC ${ACL_LIBRARY})
            if(MSPROFILER_LIBRARY)
                message(STATUS "  msprofiler library: ${MSPROFILER_LIBRARY}")
                target_link_libraries(tracesmith-capture PUBLIC ${MSPROFILER_LIBRARY})
            endif()
            
            # Add rpath for runtime (including driver path)
            set_target_properties(tracesmith-capture PROPERTIES
                BUILD_RPATH "${ASCEND_LIB_PATH};${ASCEND_HOME}/lib64;${ASCEND_DRIVER_LIB_PATH}"
                INSTALL_RPATH "${ASCEND_LIB_PATH};${ASCEND_HOME}/lib64;${ASCEND_DRIVER_LIB_PATH}"
            )
            
            target_compile_definitions(tracesmith-capture PUBLIC TRACESMITH_ENABLE_ASCEND)
        else()
            message(WARNING "Ascend ACL library not found")
            set(TRACESMITH_ENABLE_ASCEND OFF PARENT_SCOPE)
        endif()
    else()
        message(WARNING "Ascend SDK not found. Set ASCEND_HOME to the correct path.")
        set(TRACESMITH_ENABLE_ASCEND OFF PARENT_SCOPE)
    endif()
endif()
