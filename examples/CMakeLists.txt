# Examples

add_executable(basic_example
    basic_example.cpp
)

target_link_libraries(basic_example PRIVATE
    tracesmith-common
    tracesmith-format
    tracesmith-capture
)

add_executable(phase2_example
    phase2_example.cpp
)

target_link_libraries(phase2_example PRIVATE
    tracesmith-common
    tracesmith-format
    tracesmith-capture
    tracesmith-state
)

add_executable(phase3_example
    phase3_example.cpp
)

target_link_libraries(phase3_example PRIVATE
    tracesmith-common
    tracesmith-format
    tracesmith-capture
    tracesmith-state
)

add_executable(phase4_example
    phase4_example.cpp
)

target_link_libraries(phase4_example PRIVATE
    tracesmith-common
    tracesmith-format
    tracesmith-capture
    tracesmith-state
    tracesmith-replay
)

# Enhanced Perfetto Export Test
add_executable(perfetto_enhanced_test
    perfetto_enhanced_test.cpp
)

target_link_libraries(perfetto_enhanced_test PRIVATE
    tracesmith-common
    tracesmith-format
    tracesmith-capture
    tracesmith-state
)

# CUPTI Example (requires CUDA)
if(TRACESMITH_ENABLE_CUDA AND CUPTI_FOUND)
    # Enable CUDA language for .cu files
    enable_language(CUDA)
    
    # cupti_example.cpp contains __global__ kernels, treat as CUDA source
    set_source_files_properties(cupti_example.cpp PROPERTIES LANGUAGE CUDA)
    
    add_executable(cupti_example
        cupti_example.cpp
    )
    
    target_link_libraries(cupti_example PRIVATE
        tracesmith-common
        tracesmith-format
        tracesmith-capture
        tracesmith-state
        CUPTI::cupti
    )
    
    target_compile_definitions(cupti_example PRIVATE TRACESMITH_ENABLE_CUDA)
    
    # CUDA architecture (adjust for your GPU)
    set_target_properties(cupti_example PROPERTIES
        CUDA_SEPARABLE_COMPILATION ON
        CUDA_ARCHITECTURES "70;75;80;86;89;90"
    )
endif()

# Metal Example (requires macOS)
if(TRACESMITH_ENABLE_METAL AND APPLE)
    add_executable(metal_example
        metal_example.mm
    )
    
    target_link_libraries(metal_example PRIVATE
        tracesmith-common
        tracesmith-format
        tracesmith-capture
        tracesmith-state
        "-framework Metal"
        "-framework Foundation"
    )
    
    target_compile_definitions(metal_example PRIVATE TRACESMITH_ENABLE_METAL)
    
    # Enable Objective-C++ and ARC
    set_source_files_properties(metal_example.mm PROPERTIES
        COMPILE_FLAGS "-fobjc-arc"
    )
endif()
