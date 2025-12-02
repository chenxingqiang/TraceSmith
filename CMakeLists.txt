cmake_minimum_required(VERSION 3.16)
project(TraceSmith 
    VERSION 0.1.0
    DESCRIPTION "GPU Profiling & Replay System"
    LANGUAGES CXX
)

# C++ Standard
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Build options
option(TRACESMITH_BUILD_TESTS "Build unit tests" ON)
option(TRACESMITH_BUILD_EXAMPLES "Build examples" ON)
option(TRACESMITH_BUILD_CLI "Build CLI tools" ON)
option(TRACESMITH_ENABLE_CUDA "Enable CUDA/CUPTI support" OFF)
option(TRACESMITH_ENABLE_ROCM "Enable ROCm support" OFF)
option(TRACESMITH_ENABLE_METAL "Enable Metal support" OFF)

# Output directories
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)

# Compiler warnings
if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang|AppleClang")
    add_compile_options(-Wall -Wextra -Wpedantic)
endif()

# Include directories
include_directories(${CMAKE_SOURCE_DIR}/include)

# Third-party dependencies
add_subdirectory(third_party)

# Core libraries
add_subdirectory(src/common)
add_subdirectory(src/format)
add_subdirectory(src/capture)
add_subdirectory(src/state)
add_subdirectory(src/replay)

# CLI tools
if(TRACESMITH_BUILD_CLI)
    add_subdirectory(cli)
endif()

# Tests
if(TRACESMITH_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()

# Examples
if(TRACESMITH_BUILD_EXAMPLES)
    add_subdirectory(examples)
endif()

# Installation
include(GNUInstallDirs)

install(DIRECTORY include/tracesmith
    DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
)

# Package configuration
include(CMakePackageConfigHelpers)

configure_package_config_file(
    ${CMAKE_SOURCE_DIR}/cmake/TraceSmithConfig.cmake.in
    ${CMAKE_BINARY_DIR}/TraceSmithConfig.cmake
    INSTALL_DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/TraceSmith
)

write_basic_package_version_file(
    ${CMAKE_BINARY_DIR}/TraceSmithConfigVersion.cmake
    VERSION ${PROJECT_VERSION}
    COMPATIBILITY SameMajorVersion
)

install(FILES
    ${CMAKE_BINARY_DIR}/TraceSmithConfig.cmake
    ${CMAKE_BINARY_DIR}/TraceSmithConfigVersion.cmake
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/TraceSmith
)

# Summary
message(STATUS "")
message(STATUS "TraceSmith v${PROJECT_VERSION} Configuration:")
message(STATUS "  Build type:     ${CMAKE_BUILD_TYPE}")
message(STATUS "  Build tests:    ${TRACESMITH_BUILD_TESTS}")
message(STATUS "  Build examples: ${TRACESMITH_BUILD_EXAMPLES}")
message(STATUS "  Build CLI:      ${TRACESMITH_BUILD_CLI}")
message(STATUS "  CUDA support:   ${TRACESMITH_ENABLE_CUDA}")
message(STATUS "  ROCm support:   ${TRACESMITH_ENABLE_ROCM}")
message(STATUS "  Metal support:  ${TRACESMITH_ENABLE_METAL}")
message(STATUS "")
