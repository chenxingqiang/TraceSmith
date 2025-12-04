# TraceSmith Cluster Module
# Multi-GPU profiling support

set(CLUSTER_SOURCES
    gpu_topology.cpp
    multi_gpu_profiler.cpp
    time_sync.cpp
    nccl_tracker.cpp
)

add_library(tracesmith-cluster STATIC ${CLUSTER_SOURCES})

# Required for linking into Python extension (.so)
set_target_properties(tracesmith-cluster PROPERTIES
    POSITION_INDEPENDENT_CODE ON
)

target_include_directories(tracesmith-cluster PUBLIC
    ${CMAKE_SOURCE_DIR}/include
)

target_link_libraries(tracesmith-cluster PUBLIC
    tracesmith-common
    tracesmith-capture
)

# NVML support for GPU topology discovery
if(TRACESMITH_ENABLE_CUDA)
    # Find NVML (comes with CUDA toolkit)
    find_path(NVML_INCLUDE_DIR nvml.h
        HINTS
        ${CUDA_TOOLKIT_ROOT_DIR}/include
        /usr/local/cuda/include
        /opt/cuda/include
    )
    
    find_library(NVML_LIBRARY nvidia-ml
        HINTS
        ${CUDA_TOOLKIT_ROOT_DIR}/lib64
        ${CUDA_TOOLKIT_ROOT_DIR}/lib
        /usr/local/cuda/lib64
        /opt/cuda/lib64
        /usr/lib/x86_64-linux-gnu
    )
    
    if(NVML_INCLUDE_DIR AND NVML_LIBRARY)
        message(STATUS "NVML found: ${NVML_LIBRARY}")
        target_include_directories(tracesmith-cluster PRIVATE ${NVML_INCLUDE_DIR})
        target_link_libraries(tracesmith-cluster PRIVATE ${NVML_LIBRARY})
        target_compile_definitions(tracesmith-cluster PRIVATE TRACESMITH_HAS_NVML)
    else()
        message(WARNING "NVML not found, GPU topology discovery will be limited")
    endif()
    
    # Link CUDA runtime
    if(TARGET CUDA::cudart)
        target_link_libraries(tracesmith-cluster PRIVATE CUDA::cudart)
    endif()
    
    target_compile_definitions(tracesmith-cluster PRIVATE TRACESMITH_ENABLE_CUDA)
endif()

# Install
install(TARGETS tracesmith-cluster
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
)

install(DIRECTORY ${CMAKE_SOURCE_DIR}/include/tracesmith/cluster
    DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/tracesmith
)

