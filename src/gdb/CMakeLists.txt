# TraceSmith GDB RSP Backend Library

add_library(tracesmith-gdb
    gdb_types.cpp
    rsp_packet.cpp
    gpu_debug_engine.cpp
    process_controller.cpp
    rsp_handler.cpp
)

target_include_directories(tracesmith-gdb PUBLIC
    ${CMAKE_SOURCE_DIR}/include
)

target_link_libraries(tracesmith-gdb PUBLIC
    tracesmith-common
    tracesmith-format
    tracesmith-capture
    tracesmith-state
    tracesmith-replay
)

# Platform-specific libraries for ptrace
if(UNIX AND NOT APPLE)
    # Linux-specific
    target_compile_definitions(tracesmith-gdb PRIVATE TRACESMITH_PLATFORM_LINUX)
elseif(APPLE)
    # macOS-specific (uses task_for_pid instead of ptrace)
    target_compile_definitions(tracesmith-gdb PRIVATE TRACESMITH_PLATFORM_MACOS)
endif()

# Threads support
find_package(Threads REQUIRED)
target_link_libraries(tracesmith-gdb PRIVATE Threads::Threads)

# Installation
install(TARGETS tracesmith-gdb
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
)

# GDB Server executable
add_executable(tracesmith-gdbserver
    ${CMAKE_SOURCE_DIR}/tools/tracesmith-gdbserver.cpp
)

target_link_libraries(tracesmith-gdbserver PRIVATE
    tracesmith-gdb
)

install(TARGETS tracesmith-gdbserver
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)
