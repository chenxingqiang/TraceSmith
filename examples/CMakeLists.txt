# Examples
# ========
# This directory contains example programs demonstrating TraceSmith features.

# ----------------------------------------------------------------------------
# Basic Example - Platform detection, events, SBT I/O, Perfetto export
# ----------------------------------------------------------------------------
add_executable(basic_example
    basic_example.cpp
)

target_link_libraries(basic_example PRIVATE
    tracesmith-common
    tracesmith-format
    tracesmith-capture
    tracesmith-state
)

# ----------------------------------------------------------------------------
# Phase 2 Example - Stack capture, instruction stream, dependency analysis
# ----------------------------------------------------------------------------
add_executable(phase2_example
    phase2_example.cpp
)

target_link_libraries(phase2_example PRIVATE
    tracesmith-common
    tracesmith-format
    tracesmith-capture
    tracesmith-state
)

# ----------------------------------------------------------------------------
# Phase 3 Example - GPU state machine, timeline builder, visualization
# ----------------------------------------------------------------------------
add_executable(phase3_example
    phase3_example.cpp
)

target_link_libraries(phase3_example PRIVATE
    tracesmith-common
    tracesmith-format
    tracesmith-capture
    tracesmith-state
)

# ----------------------------------------------------------------------------
# Phase 4 Example - Replay engine, determinism checking
# ----------------------------------------------------------------------------
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

# ----------------------------------------------------------------------------
# Memory Profiler Example - REAL GPU memory tracking, leak detection
# Requires CUDA for real cudaMalloc/cudaFree tracking
# ----------------------------------------------------------------------------
if(CUDA_EXAMPLES_ENABLED)
    add_executable(memory_profiler_example
        memory_profiler_example.cpp
    )
    set_source_files_properties(memory_profiler_example.cpp PROPERTIES LANGUAGE CUDA)
    target_link_libraries(memory_profiler_example PRIVATE
        tracesmith-common
        tracesmith-capture
        CUDA::cudart
    )
    target_compile_definitions(memory_profiler_example PRIVATE TRACESMITH_ENABLE_CUDA)
endif()

# ----------------------------------------------------------------------------
# Stack Capture Example - Call stack capture, symbol resolution
# ----------------------------------------------------------------------------
add_executable(stack_capture_example
    stack_capture_example.cpp
)

target_link_libraries(stack_capture_example PRIVATE
    tracesmith-common
    tracesmith-capture
)

# ----------------------------------------------------------------------------
# XRay Example - LLVM XRay trace importing
# ----------------------------------------------------------------------------
add_executable(xray_example
    xray_example.cpp
)

target_link_libraries(xray_example PRIVATE
    tracesmith-common
    tracesmith-format
    tracesmith-state
)

# ----------------------------------------------------------------------------
# Enhanced Perfetto Export Test
# ----------------------------------------------------------------------------
add_executable(perfetto_enhanced_test
    perfetto_enhanced_test.cpp
)

target_link_libraries(perfetto_enhanced_test PRIVATE
    tracesmith-common
    tracesmith-format
    tracesmith-capture
    tracesmith-state
)

# ----------------------------------------------------------------------------
# Kineto Schema Test - PyTorch profiler compatibility
# ----------------------------------------------------------------------------
add_executable(kineto_schema_test
    kineto_schema_test.cpp
)

target_link_libraries(kineto_schema_test PRIVATE
    tracesmith-common
    tracesmith-format
    tracesmith-capture
    tracesmith-state
)

# ----------------------------------------------------------------------------
# Perfetto ProtoZero Export Test
# ----------------------------------------------------------------------------
add_executable(perfetto_proto_test
    perfetto_proto_test.cpp
)

target_link_libraries(perfetto_proto_test PRIVATE
    tracesmith-common
    tracesmith-format
    tracesmith-capture
    tracesmith-state
)

# ----------------------------------------------------------------------------
# Real-time Tracing Example (v0.3.0) - Requires CUDA for real GPU workload
# ----------------------------------------------------------------------------
if(CUDA_EXAMPLES_ENABLED)
    add_executable(realtime_tracing_example
        realtime_tracing_example.cpp
    )
    set_source_files_properties(realtime_tracing_example.cpp PROPERTIES LANGUAGE CUDA)
    target_link_libraries(realtime_tracing_example PRIVATE
        tracesmith-common
        tracesmith-format
        tracesmith-capture
        tracesmith-state
        CUDA::cudart
    )
    target_compile_definitions(realtime_tracing_example PRIVATE TRACESMITH_ENABLE_CUDA)
endif()

# ----------------------------------------------------------------------------
# Counter Track Visualization Example (v0.5.0)
# ----------------------------------------------------------------------------
add_executable(counter_track_example
    counter_track_example.cpp
)

target_link_libraries(counter_track_example PRIVATE
    tracesmith-common
    tracesmith-format
    tracesmith-capture
    tracesmith-state
)

# ----------------------------------------------------------------------------
# Frame Capture Example (RenderDoc-inspired) - v0.5.0
# ----------------------------------------------------------------------------
add_executable(frame_capture_example
    frame_capture_example.cpp
)

target_link_libraries(frame_capture_example PRIVATE
    tracesmith-common
    tracesmith-replay
)

# ----------------------------------------------------------------------------
# Link Perfetto SDK if enabled
# ----------------------------------------------------------------------------
if(TRACESMITH_USE_PERFETTO_SDK)
    target_link_libraries(perfetto_proto_test PRIVATE perfetto_sdk)
    target_link_libraries(basic_example PRIVATE perfetto_sdk)
    target_link_libraries(phase3_example PRIVATE perfetto_sdk)
    if(CUDA_EXAMPLES_ENABLED)
        target_link_libraries(realtime_tracing_example PRIVATE perfetto_sdk)
    endif()
endif()

# ----------------------------------------------------------------------------
# CUPTI Example (requires CUDA)
# ----------------------------------------------------------------------------
if(TRACESMITH_ENABLE_CUDA AND CUPTI_FOUND)
    # Check if CUDA compiler is available before enabling
    include(CheckLanguage)
    check_language(CUDA)
    if(NOT CMAKE_CUDA_COMPILER)
        message(WARNING "CUDA compiler not found, skipping CUDA examples")
        set(CUDA_EXAMPLES_ENABLED OFF)
    else()
        enable_language(CUDA)
        set(CUDA_EXAMPLES_ENABLED ON)
    endif()
else()
    set(CUDA_EXAMPLES_ENABLED OFF)
endif()

if(CUDA_EXAMPLES_ENABLED)
    
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

# ----------------------------------------------------------------------------
# Metal Example (requires macOS)
# ----------------------------------------------------------------------------
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

# ----------------------------------------------------------------------------
# MetaX MACA Examples (requires MetaX GPU)
# ----------------------------------------------------------------------------
if(TRACESMITH_ENABLE_MACA AND MACA_ROOT)
    # MetaX basic example
    add_executable(metax_example
        metax_example.cpp
    )
    
    target_include_directories(metax_example PRIVATE
        ${MACA_ROOT}/include
        ${MACA_ROOT}/include/mcr
    )
    
    target_link_libraries(metax_example PRIVATE
        tracesmith-common
        tracesmith-format
        tracesmith-capture
        tracesmith-state
    )
    
    # MetaX benchmark example
    add_executable(metax_benchmark
        metax_benchmark.cpp
    )

    target_include_directories(metax_benchmark PRIVATE
        ${MACA_ROOT}/include
        ${MACA_ROOT}/include/mcr
    )

    target_link_libraries(metax_benchmark PRIVATE
        tracesmith-common
        tracesmith-format
        tracesmith-capture
        tracesmith-state
    )
    
    # MetaX cluster profiling example
    add_executable(metax_cluster_example
        metax_cluster_example.cpp
    )

    target_include_directories(metax_cluster_example PRIVATE
        ${MACA_ROOT}/include
        ${MACA_ROOT}/include/mcr
    )

    target_link_libraries(metax_cluster_example PRIVATE
        tracesmith-common
        tracesmith-format
        tracesmith-capture
        tracesmith-state
        tracesmith-cluster
    )
endif()

# ----------------------------------------------------------------------------
# Goal Validation Example - Validates all project goals from PLANNING.md
# ----------------------------------------------------------------------------
add_executable(goal_validation_example
    goal_validation_example.cpp
)

target_link_libraries(goal_validation_example PRIVATE
    tracesmith-common
    tracesmith-format
    tracesmith-capture
    tracesmith-state
    tracesmith-replay
)

# ----------------------------------------------------------------------------
# Benchmark: 10K Call Stacks - Validates core goal
# "Capture 10K+ instruction-level GPU call stacks without interrupting business"
# Uses REAL CUDA kernels + CUPTI profiling
# ----------------------------------------------------------------------------
if(CUDA_EXAMPLES_ENABLED)
    # Build with CUDA support for real GPU profiling
    add_executable(benchmark_10k_stacks
        benchmark_10k_stacks.cpp
    )
    set_source_files_properties(benchmark_10k_stacks.cpp PROPERTIES LANGUAGE CUDA)
    target_link_libraries(benchmark_10k_stacks PRIVATE
        tracesmith-common
        tracesmith-format
        tracesmith-capture
        CUDA::cudart
        ${CUPTI_LIBRARY}
    )
    target_compile_definitions(benchmark_10k_stacks PRIVATE TRACESMITH_ENABLE_CUDA)
else()
    # Build without CUDA - will show error message at runtime
    add_executable(benchmark_10k_stacks
        benchmark_10k_stacks.cpp
    )
    target_link_libraries(benchmark_10k_stacks PRIVATE
        tracesmith-common
        tracesmith-format
        tracesmith-capture
    )
endif()

# ----------------------------------------------------------------------------
# Multi-GPU Example (v0.7.0) - Requires CUDA
# ----------------------------------------------------------------------------
if(CUDA_EXAMPLES_ENABLED)
    add_executable(multi_gpu_example
        multi_gpu_example.cpp
    )
    set_source_files_properties(multi_gpu_example.cpp PROPERTIES LANGUAGE CUDA)
    target_link_libraries(multi_gpu_example PRIVATE
        tracesmith-common
        tracesmith-format
        tracesmith-capture
        tracesmith-state
        tracesmith-cluster
        CUDA::cudart
        ${CUPTI_LIBRARY}
    )
    target_compile_definitions(multi_gpu_example PRIVATE TRACESMITH_ENABLE_CUDA)
    set_target_properties(multi_gpu_example PROPERTIES
        CUDA_SEPARABLE_COMPILATION ON
        CUDA_ARCHITECTURES "70;75;80;86;89;90"
    )
endif()

# ----------------------------------------------------------------------------
# Install Python example
# ----------------------------------------------------------------------------
install(FILES python_example.py
    DESTINATION share/tracesmith/examples
    PERMISSIONS OWNER_READ OWNER_WRITE OWNER_EXECUTE
                GROUP_READ GROUP_EXECUTE
                WORLD_READ WORLD_EXECUTE
)

# ----------------------------------------------------------------------------
# Summary message
# ----------------------------------------------------------------------------
message(STATUS "TraceSmith Examples:")
message(STATUS "  Core examples (no GPU required):")
message(STATUS "    - basic_example (Platform detection, SBT I/O, Perfetto export)")
message(STATUS "    - phase2_example (Stack capture, instruction stream)")
message(STATUS "    - phase3_example (GPU state machine, timeline)")
message(STATUS "    - phase4_example (Replay engine)")
message(STATUS "    - stack_capture_example (Call stack capture)")
message(STATUS "    - xray_example (LLVM XRay import)")
message(STATUS "    - perfetto_enhanced_test (Enhanced Perfetto export)")
message(STATUS "    - kineto_schema_test (PyTorch compatibility)")
message(STATUS "    - perfetto_proto_test (ProtoZero export)")
message(STATUS "    - counter_track_example (Performance counters)")
message(STATUS "    - frame_capture_example (RenderDoc-style capture)")
message(STATUS "    - goal_validation_example (Validates all PLANNING.md goals)")
if(CUDA_EXAMPLES_ENABLED)
    message(STATUS "  CUDA examples (NVIDIA GPU):")
    message(STATUS "    - cupti_example (NVIDIA CUPTI profiling)")
    message(STATUS "    - memory_profiler_example (Real GPU memory tracking)")
    message(STATUS "    - realtime_tracing_example (Real GPU workload tracing)")
    message(STATUS "    - benchmark_10k_stacks (10K GPU call stack capture)")
    message(STATUS "    - multi_gpu_example (Multi-GPU profiling v0.7.0)")
else()
    message(STATUS "  CUDA examples: SKIPPED (CUDA compiler not found)")
    message(STATUS "    Note: benchmark_10k_stacks built without GPU support")
endif()
if(TRACESMITH_ENABLE_METAL AND APPLE)
    message(STATUS "  Metal examples (Apple GPU):")
    message(STATUS "    - metal_example (Apple Metal profiling)")
endif()
if(TRACESMITH_ENABLE_MACA)
    message(STATUS "  MetaX examples (MetaX GPU):")
    message(STATUS "    - metax_example (MetaX MCPTI profiling)")
    message(STATUS "    - metax_benchmark (MetaX memory bandwidth test)")
    message(STATUS "    - metax_cluster_example (MetaX multi-GPU cluster profiling)")
endif()
message(STATUS "  Python examples:")
message(STATUS "    - python_example.py (Python API demonstration)")
