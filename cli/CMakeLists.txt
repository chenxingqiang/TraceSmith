# CLI tools

# Check if CUDA is available for benchmark command
if(TRACESMITH_ENABLE_CUDA AND CUPTI_FOUND)
    include(CheckLanguage)
    check_language(CUDA)
    if(CMAKE_CUDA_COMPILER)
        enable_language(CUDA)
        set(CLI_CUDA_ENABLED ON)
    else()
        set(CLI_CUDA_ENABLED OFF)
        message(STATUS "CLI: CUDA compiler not found, benchmark command will be limited")
    endif()
else()
    set(CLI_CUDA_ENABLED OFF)
endif()

add_executable(tracesmith
    main.cpp
)

# If CUDA is enabled, compile main.cpp as CUDA source
if(CLI_CUDA_ENABLED)
    set_source_files_properties(main.cpp PROPERTIES LANGUAGE CUDA)
    target_compile_definitions(tracesmith PRIVATE TRACESMITH_ENABLE_CUDA)
    target_link_libraries(tracesmith PRIVATE
        tracesmith-common
        tracesmith-format
        tracesmith-capture
        tracesmith-state
        tracesmith-replay
        CUDA::cudart
        ${CUPTI_LIBRARY}
    )
    set_target_properties(tracesmith PROPERTIES
        CUDA_SEPARABLE_COMPILATION ON
        CUDA_ARCHITECTURES "70;75;80;86;89;90"
    )
else()
    target_link_libraries(tracesmith PRIVATE
        tracesmith-common
        tracesmith-format
        tracesmith-capture
        tracesmith-state
        tracesmith-replay
    )
endif()

# Install CLI
install(TARGETS tracesmith
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)
